// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include <sys/mman.h>

#include "disk.hh"
#include "hash.hh"

using namespace filesystem ;

// ENUM macro does not work inside namespace's
ENUM( CanonState
,	First
,	Empty
,	Dot
,	DotDot
,	Plain
)

namespace Disk {

	bool is_canon(::string const& name) {
		bool       accept_dot_dot = true              ;
		CanonState state          = CanonState::First ;
		for( char c : name ) {
			switch (c) {
				case '/' :
					switch (state) {
						case CanonState::Empty  :                      return false ;
						case CanonState::Dot    :                      return false ;
						case CanonState::DotDot : if (!accept_dot_dot) return false ; break ;
						case CanonState::First  :                                             // seen from /, First is like Plain
						case CanonState::Plain  : accept_dot_dot = false ;            break ; // .. is only accepted as relative prefix
					DF}
					state = CanonState::Empty ;
				break ;
				case '.' :
					switch (state) {
						case CanonState::First  :                                             // seen from ., First is like Empty
						case CanonState::Empty  : state = CanonState::Dot    ; break ;
						case CanonState::Dot    : state = CanonState::DotDot ; break ;
						case CanonState::DotDot : state = CanonState::Plain  ; break ;
						case CanonState::Plain  :                              break ;
					DF}
				break ;
				default :
					state = CanonState::Plain ;
			}
		}
		switch (state) {
			case CanonState::First  :                                                         // an empty name
			case CanonState::Empty  : return true  ;                                          // a directory ending with /
			case CanonState::Dot    : return false ;
			case CanonState::DotDot : return false ;
			case CanonState::Plain  : return true  ;
		DF}
		return true ;
	}

	vector_s lst_dir( Fd at , ::string const& dir , ::string const& prefix ) {
		Fd dir_fd = at ;
		if      (+dir       ) dir_fd = ::openat( at , dir.c_str() , O_RDONLY|O_DIRECTORY ) ;
		else if (at==Fd::Cwd) dir_fd = ::openat( at , "."         , O_RDONLY|O_DIRECTORY ) ;
		if (!dir_fd) throw to_string("cannot open dir ",at==Fd::Cwd?"":to_string('@',at,':'),dir) ;
		//
		DIR* dir_fp = ::fdopendir(dir_fd) ;
		if (!dir_fp) throw to_string("cannot list dir ",at==Fd::Cwd?"":to_string('@',at,':'),dir) ;
		//
		::vector_s res ;
		while ( struct dirent* entry = ::readdir(dir_fp) ) {
			if (entry->d_name[0]!='.') goto Ok  ;
			if (entry->d_name[1]==0  ) continue ; // ignore .
			if (entry->d_name[1]!='.') goto Ok  ;
			if (entry->d_name[2]==0  ) continue ; // ignore ..
		Ok :
			res.emplace_back(prefix+entry->d_name) ;
		}
		if (dir_fd!=at) ::closedir(dir_fp) ;
		return res ;
	}

	void unlnk_inside( Fd at , ::string const& dir ) {
		::string dir_s = +dir ? dir+'/' : ""s ;
		for( ::string const& f : lst_dir(at,dir,dir_s) ) unlnk(at,f,true/*dir_ok*/) ;
	}

	bool/*done*/ unlnk( Fd at , ::string const& file , bool dir_ok ) {
		if (::unlinkat(at,file.c_str(),0)==0           ) return true /*done*/ ;
		if (errno==ENOENT                              ) return false/*done*/ ;
		if (!dir_ok                                    ) throw to_string("cannot unlink "     ,file) ;
		if (errno!=EISDIR                              ) throw to_string("cannot unlink file ",file) ;
		unlnk_inside(at,file) ;
		if (::unlinkat(at,file.c_str(),AT_REMOVEDIR)!=0) throw to_string("cannot unlink dir " ,file) ;
		return true/*done*/ ;
	}

	bool/*done*/ uniquify( Fd at , ::string const& file ) {                                 // uniquify file so as to ensure modifications do not alter other hard links
		SWEAR(+file) ;                                                                      // cannot unlink at without file
		const char*   f   = file.c_str() ;
		const char*   msg = nullptr      ;
		struct ::stat st  ;
		{
			int     src     = ::fstatat(at,f,&st,AT_SYMLINK_NOFOLLOW)                ; if (src<0         )                                     return false/*done*/ ;
			/**/                                                                       if (st.st_nlink<=1)                                     return false/*done*/ ;
			int     rfd     = ::openat(at,f,O_RDONLY|O_NOFOLLOW)                     ; if (rfd<0         ) { msg = "cannot open for reading" ; goto Bad             ; }
			void*   content = ::mmap(nullptr,st.st_size,PROT_READ,MAP_PRIVATE,rfd,0) ; if (!content      ) { msg = "cannot map"              ; goto Bad             ; }
			int     urc     = ::unlinkat(at,f,0)                                     ; if (urc<0         ) { msg = "cannot unlink"           ; goto Bad             ; }
			int     wfd     = ::openat(at,f,O_WRONLY|O_CREAT,st.st_mode&07777)       ; if (wfd<0         ) { msg = "cannot open for writing" ; goto Bad             ; }
			ssize_t cnt     = 0/*garbage*/                                           ;
			//
			for( off_t pos=0 ; pos<st.st_size ; pos+=cnt ) {
				cnt = ::write( wfd , reinterpret_cast<char const*>(content)+cnt , st.st_size-pos)  ;
				if (cnt<=0) throw to_string("cannot write to ",file) ;
			}
			//
			struct ::timespec times[2] = { {.tv_sec=0,.tv_nsec=UTIME_OMIT} , st.st_mtim } ;
			::futimens(wfd,times) ;                                                         // maintain original date
			//
			::close(wfd) ;
			::munmap(content,st.st_size) ;
			::close(rfd) ;
			return true/*done*/ ;
		}
	Bad :
		if (at==Fd::Cwd) throw to_string(msg,' ',         file) ;
		else             throw to_string(msg," @",+at,':',file) ;
	}

	void rmdir( Fd at , ::string const& dir ) {
		if (::unlinkat(at,dir.c_str(),AT_REMOVEDIR)!=0) throw to_string("cannot rmdir ",dir) ;
	}

	::vector_s read_lines(::string const& filename) {
		::ifstream file_stream{filename} ;
		if (!file_stream) return {} ;
		//
		::vector_s res            ;
		char       line[PATH_MAX] ;
		while (file_stream.getline(line,sizeof(line))) res.push_back(line) ;
		return res ;
	}

	::string read_content(::string const& file) {
		::ifstream is{file} ;
		if (!is                  ) throw to_string("file not found : ",file) ;
		if (::istream::sentry(is)) return to_string(is.rdbuf())              ; // /!\ rdbuf() fails on an empty file
		else                       return {}                                 ;
	}

	void write_lines( ::string const& file , ::vector_s const& lines ) {
		OFStream file_stream{file} ;
		if (+lines) SWEAR_PROD(bool(file_stream)) ;
		for( ::string const& l : lines ) file_stream << l << '\n' ;
	}

	void write_content( ::string const& file , ::string const& content ) {
		OFStream file_stream{file} ;
		if (+content) SWEAR_PROD(bool(file_stream)) ;
		file_stream << content ;
	}

	static void _walk( ::vector_s& res , Fd at , ::string const& file , ::string const& prefix ) {
		if (FileInfo(at,file).tag()!=FileTag::Dir) {
			res.push_back(prefix) ;
			return ;
		}
		::vector_s lst ;
		try                     { lst = lst_dir(at,file) ; }
		catch (::string const&) { return ;                 } // list only accessible files
		::string   file_s   = file  +'/'       ;
		::string   prefix_s = prefix+'/'       ;
		for( ::string const& f : lst ) _walk( res , at,file_s+f , prefix_s+f ) ;
	}
	::vector_s walk( Fd at , ::string const& file , ::string const& prefix ) {
		::vector_s res ;
		_walk( res , at , file , prefix ) ;
		return res ;
	}

	static int/*n_dirs*/ _mkdir( Fd at , ::string const& dir , NfsGuard* nfs_guard , bool multi , bool unlnk_ok ) {
		::vector_s  to_mk { dir }   ;
		const char* msg   = nullptr ;
		int         res   = 0       ;
		while (+to_mk) {
			::string const& d = to_mk.back() ;                                                             // parents are after children in to_mk
			if (nfs_guard) { SWEAR(at==Fd::Cwd) ; nfs_guard->change(d) ; }
			if (::mkdirat(at,d.c_str(),0777)==0) {
				res++ ;
				to_mk.pop_back() ;
				continue ;
			}                                                                                              // done
			switch (errno) {
				case EEXIST :
					if ( unlnk_ok && !is_dir(at,d) ) unlnk(at,d) ;                                         // retry
					else                             to_mk.pop_back() ;                                    // done
				break ;
				case ENOENT  :
				case ENOTDIR :
					if (                           !multi ) { msg = "cannot create dir"     ; goto Bad ; }
					if ( ::string dd=dir_name(d) ; +dd    )   to_mk.push_back(::move(dd)) ;                // retry after parent is created
					else                                    { msg = "cannot create top dir" ; goto Bad ; } // if ENOTDIR, a parent is not a dir, it will be fixed up
				break  ;
				default :
					msg = "cannot create dir" ;
				Bad :
					if (at==Fd::Cwd) throw to_string(msg,' ' ,       d) ;
					else             throw to_string(msg," @",at,':',d) ;
			}
		}
		return res ;
	}
	int/*n_dirs*/ mkdir( Fd at , ::string const& dir ,                       bool multi , bool unlnk_ok ) { return _mkdir(at,dir,nullptr   ,multi,unlnk_ok) ; }
	int/*n_dirs*/ mkdir( Fd at , ::string const& dir , NfsGuard& nfs_guard , bool multi , bool unlnk_ok ) { return _mkdir(at,dir,&nfs_guard,multi,unlnk_ok) ; }

	::string dir_name(::string const& file) {
		size_t sep = file.rfind('/') ;
		if (sep==Npos) return {}                 ;
		else           return file.substr(0,sep) ;
	}

	::string base_name(::string const& file) {
		size_t sep = file.rfind('/') ;
		if (sep!=Npos) return file.substr(sep+1) ;
		else           return file               ;
	}

	void dir_guard( Fd at , ::string const& file ) {
		::string dir = dir_name(file) ;
		if (+dir) mkdir(at,dir) ;
	}

	::string mk_lcl( ::string const& file , ::string const& dir_s ) {
		SWEAR( is_abs(file) == is_abs_s(dir_s) , file , dir_s ) ;
		SWEAR( !dir_s || dir_s.back()=='/'     ,        dir_s ) ;
		size_t last_slash1 = 0 ;
		for( size_t i=0 ; i<file.size() ; i++ ) {
			if (file[i]!=dir_s[i]) break ;
			if (file[i]=='/'     ) last_slash1 = i+1 ;
		}
		::string res ;
		for( char c : ::string_view(dir_s).substr(last_slash1) ) if (c=='/') res += "../" ;
		res += ::string_view(file).substr(last_slash1) ;
		return res ;
	}

	::string mk_glb( ::string const& file , ::string const& dir_s ) {
		if (is_abs(file)) return file ;
		::string_view d_sv = dir_s ;
		::string_view f_v  = file  ;
		for(; f_v.starts_with("../") ; f_v = f_v.substr(3) ) {
			SWEAR(+d_sv) ;
			d_sv = d_sv.substr(0,d_sv.size()-1) ;                                         // suppress ending /
			size_t last_slash = d_sv.rfind('/') ;
			if (last_slash==Npos) { SWEAR(+d_sv) ; d_sv = d_sv.substr(0,0           ) ; }
			else                  {                d_sv = d_sv.substr(0,last_slash+1) ; } // keep new ending /
		}
		return to_string(d_sv,f_v) ;
	}

	::string _localize( ::string const& txt , ::string const& dir_s , size_t first_file ) {
		size_t   pos = first_file        ;
		::string res = txt.substr(0,pos) ;
		while (pos!=Npos) {
			pos++ ;                                                  // clobber marker
			SWEAR(txt.size()>=pos+sizeof(FileNameIdx)) ;             // ensure we have enough room to find file length
			FileNameIdx len = decode_int<FileNameIdx>(&txt[pos]) ;
			pos += sizeof(FileNameIdx) ;                             // clobber file length
			SWEAR(txt.size()>=pos+len) ;                             // ensure we have enough room to read file
			res += mk_printable(mk_rel(txt.substr(pos,len),dir_s)) ;
			pos += len ;
			size_t new_pos = txt.find(FileMrkr,pos) ;
			res += txt.substr(pos,new_pos-pos) ;
			pos  = new_pos ;
		}
		return res ;
	}

	//
	// FileInfo
	//

	::ostream& operator<<( ::ostream& os , FileInfo const& fi ) {
		/**/     os<< "FileInfo("          ;
		if (+fi) os<<fi.sz <<','<< fi.date ;
		return   os<<')'                   ;
	}

	FileInfo::FileInfo( Fd at , ::string const& name , bool no_follow ) {
		Stat st ;
		if (::fstatat( at , name.c_str() , &st , AT_EMPTY_PATH|(no_follow?AT_SYMLINK_NOFOLLOW:0) )!=0) return ;
		//
		FileTag tag ;
		if (S_ISREG(st.st_mode)) {
			if      (st.st_mode&S_IXUSR) tag = FileTag::Exe   ;
			else if (!st.st_size       ) tag = FileTag::Empty ;
			else                         tag = FileTag::Reg   ;
		} else if (S_ISLNK(st.st_mode)) {
			tag = FileTag::Lnk ;
		} else {
			if (S_ISDIR(st.st_mode)) date = Ddate(FileTag::Dir) ;
			return ;
		}
		sz   = st.st_size   ;
		date = Ddate(st,tag);
	}

	//
	// FileSig
	//

	::ostream& operator<<( ::ostream& os , FileSig const& sig ) {
		return os<< "FileSig(" << ::hex<<(sig._val>>NBits<FileTag>)<<::dec <<':'<< sig.tag() <<')' ;
	}

	FileSig::FileSig( FileInfo const& fi ) {
		_val = +fi.tag() ;
		if (!fi) return ;
		Hash::Xxh h ;
		h.update(fi.date) ;
		h.update(fi.sz  ) ;
		_val |= (+h.digest()<<NBits<FileTag>) ;
	}

	//
	// SigDate
	//

	::ostream& operator<<( ::ostream& os , SigDate const& sd ) { return os <<'('<< sd.sig <<','<< sd.date <<')' ; }

	//
	// FileMap
	//

	FileMap::FileMap( Fd at , ::string const& filename ) {
		_fd = open_read(at,filename) ;
		if (!_fd) return ;
		sz = FileInfo(_fd,{},false/*no_follow*/).sz ;
		if (sz) {
			data = static_cast<const uint8_t*>(::mmap( nullptr , sz , PROT_READ , MAP_PRIVATE , _fd , 0 )) ;
			if (data==MAP_FAILED) {
				_fd.detach() ;      // report error
				data = nullptr ;    // avoid garbage info
				return ;
			}
		}
		_ok = true ;
	}

	//
	// RealPath
	//

	::ostream& operator<<( ::ostream& os , RealPathEnv const& rpe ) {
		/**/                    os << "RealPathEnv(" << rpe.lnk_support ;
		if ( rpe.reliable_dirs) os << ",reliable_dirs"                  ;
		/**/                    os <<','<< rpe.root_dir                 ;
		if (+rpe.tmp_dir      ) os <<','<< rpe.tmp_dir                  ;
		if (+rpe.tmp_view     ) os <<','<< rpe.tmp_view                 ;
		if (+rpe.src_dirs_s   ) os <<','<< rpe.src_dirs_s               ;
		return                  os <<')'                                ;
	}

	::ostream& operator<<( ::ostream& os , RealPath::SolveReport const& sr ) {
		return os << "SolveReport(" << sr.real <<','<< sr.file_loc <<','<< sr.lnks <<')' ;
	}

	::ostream& operator<<( ::ostream& os , RealPath const& rp ) {
		/**/                     os << "RealPath("             ;
		if (+rp.pid            ) os << rp.pid <<','            ;
		if (+rp.has_tmp_view   ) os <<','<< *rp._tmp_view      ;
		/**/                     os <<      rp.cwd_            ;
		/**/                     os <<','<< rp._admin_dir      ;
		if (+rp._abs_src_dirs_s) os <<','<< rp._abs_src_dirs_s ;
		return                   os <<')'                      ;
	}

	void RealPath::init( RealPathEnv const& rpe , ::string&& cwd , pid_t p ) { // cwd in disk space, i.e. mapped in case of tmp mapping
		/**/               SWEAR( is_abs(rpe.root_dir) , rpe.root_dir ) ;
		/**/               SWEAR( is_abs(rpe.tmp_dir ) , rpe.tmp_dir  ) ;
		if (+rpe.tmp_view) SWEAR( is_abs(rpe.tmp_view) , rpe.tmp_view ) ;
		//
		pid           = p                                           ;
		_env          = &rpe                                        ;
		has_tmp_view  = +rpe.tmp_view                               ;
		_tmp_view     = has_tmp_view ? &rpe.tmp_view : &rpe.tmp_dir ;
		_admin_dir    = to_string(rpe.root_dir,'/',AdminDir)        ;
		cwd_          = ::move(cwd)                                 ;
		_root_dir_sz1 = _env->root_dir.size()+1                     ;
		//
		for ( ::string const& sd_s : rpe.src_dirs_s ) _abs_src_dirs_s.push_back(mk_glb(sd_s,rpe.root_dir)) ;
	}

	size_t RealPath::_find_src_idx(::string const& real) const {
		for( size_t i=0 ; i<_abs_src_dirs_s.size() ; i++ ) if (real.starts_with(_abs_src_dirs_s[i])) return i    ;
		/**/                                                                                         return Npos ;
	}

	// strong performance efforts have been made :
	// - avoid ::string copying as much as possible
	// - do not support links outside repo & tmp, except from /proc (which is meaningful)
	// - note that besides syscalls, this algo is very fast and caching intermediate results could degrade performances (checking the cache could take as long as doing the job)
	static int _get_symloop_max() {                                      // max number of links to follow before decreting it is a loop
		int res = ::sysconf(_SC_SYMLOOP_MAX) ;
		if (res>=0) return res                ;
		else        return _POSIX_SYMLOOP_MAX ;
	}
	// XXX : optimize by transforming cur into a const char*, avoiding a copy for caller
	// XXX : optimize by looking in /proc (after having opened the file) if file is a real path and provide direct answer in this case
	//       apply, as soon as we prepare to make a readlink (before that, we may lose time instead of saving), which arrives pretty soon when link support is full
	RealPath::SolveReport RealPath::solve( Fd at , ::string const& file , bool no_follow ) {
		static ::string const* const proc         = new ::string("/proc") ;
		static int             const s_n_max_lnks = _get_symloop_max()    ;
		//
		::vector_s lnks ;
		//
		::string        local_file[2] ;                                  // ping-pong used to keep a copy of input file if we must modify it (avoid upfront copy as it is rarely necessary)
		bool            exists        = true                      ;      // if false, we have seen a non-existent component and there cannot be symlinks within it
		::string const* cur           = &file                     ;      // points to the current file : input file or local copy local_file
		size_t          pos           = file[0]=='/'              ;
		::string        real          ; real.reserve(file.size()) ;      // canonical (link free, absolute, no ., .. nor empty component). Empty instead of '/'. Anticipate no link
		if (!pos) {                                                      // file is relative, meaning relative to at
			if (at==Fd::Cwd) {
				real = cwd_ ;
			} else {
				if ( pid                                             ) real = read_lnk(to_string(*proc,'/',pid,"/fd/",at.fd))  ;
				else                                                   real = read_lnk(to_string(*proc,"/self/fd/"   ,at.fd))  ;
				if ( has_tmp_view && real.starts_with(_env->tmp_dir) ) real = *_tmp_view + (real.c_str()+_env->tmp_dir.size()) ;
			}
			if (!is_abs(real) ) return {} ;                              // user code might use the strangest at, it will be an error but we must support it
			if (real.size()==1) real.clear() ;
		}
		_Dvg in_repo   { _env->root_dir , real } ;                       // keep track of where we are w.r.t. repo       , track symlinks according to lnk_support policy
		_Dvg in_tmp    { *_tmp_view     , real } ;                       // keep track of where we are w.r.t. tmp        , always track symlinks
		_Dvg in_admin  { _admin_dir     , real } ;                       // keep track of where we are w.r.t. repo/LMAKE , never track symlinks, like files in no domain
		_Dvg in_proc   { *proc          , real } ;                       // keep track of where we are w.r.t. /proc      , always track symlinks
		bool is_in_tmp = +*_tmp_view && +in_tmp  ;
		// loop INVARIANT : accessed file is real+'/'+cur->substr(pos)
		// when pos>cur->size(), we are done and result is real
		size_t   end       ;
		int      n_lnks    = 0                         ;
		::string last_lnk  ;
		bool     mapped    = has_tmp_view && is_in_tmp ;                 // if true <=> tmp mapping has been used
		for (
		;	pos <= cur->size()
		;		pos = end+1
			,	in_repo.update(_env->root_dir,real)                      // for all domains except admin, they start only when inside, i.e. the domain root is not part of the domain
			,	in_tmp .update(*_tmp_view    ,real)                      // .
			,	in_proc.update(*proc         ,real)                      // .
			,	is_in_tmp = +*_tmp_view && +in_tmp
		) {
			end = cur->find( '/', pos ) ;
			bool last = end==Npos ;
			if (last    ) end = cur->size() ;
			if (end==pos) continue ;                                     // empty component, ignore
			if ((*cur)[pos]=='.') {
				if ( end==pos+1                       ) continue ;       // component is .
				if ( end==pos+2 && (*cur)[pos+1]=='.' ) {                // component is ..
					if (+real) real.resize(real.rfind('/')) ;
					continue ;
				}
			}
			size_t    prev_real_size = real.size()                     ;
			::string& nxt            = local_file[cur!=&local_file[1]] ; // bounce, initially, when cur is neither local_file's, any buffer is ok
			real.push_back('/') ;
			real.append(*cur,pos,end-pos) ;
			in_admin.update(_admin_dir,real) ;                           // for the admin domain, it starts at itself, i.e. the admin dir is part of the domain
			if ( !exists           ) continue       ;                    // if !exists, no hope to find a symbolic link but continue cleanup of empty, . and .. components
			if ( no_follow && last ) continue       ;                    // dont care about last component if no_follow
			if ( is_in_tmp         ) goto HandleLnk ;                    // note that tmp can lie within repo or admin
			if ( +in_admin         ) continue       ;
			if ( +in_proc          ) goto HandleLnk ;
			if ( !in_repo          ) continue       ;
			//
			if ( !last && !_env->reliable_dirs )                                                                                    // at last level, dirs are rare and NFS does the coherence job
				if ( Fd dfd = ::open(real.c_str(),O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_NOATIME) ; +dfd ) {                             // sym links are rare, so this has no significant perf impact ...
					::close(dfd) ;                                                                                                  // ... and protects against NFS strange notion of coherence
					continue ;
				}
			//
			switch (_env->lnk_support) {
				case LnkSupport::None :                                 continue ;
				case LnkSupport::File : if (last) goto HandleLnk ; else continue ;                                                  // only handle sym links as last component
				case LnkSupport::Full :           goto HandleLnk ;
			DF}
		HandleLnk :
			if ( has_tmp_view && is_in_tmp ) { nxt = read_lnk( _env->tmp_dir + real.substr(_tmp_view->size()) ) ; mapped = true ; } // XXX : optimize by leveraging dir fd computed on previous loop
			else                             { nxt = read_lnk( real                                           ) ;                 }
			if ( !is_in_tmp && !in_proc ) {
				if (+in_repo) {
					if      ( real.size()<_root_dir_sz1              ) continue ;                                                                         // at repo root, no sym link to handle
					else if ( +nxt                                   ) lnks.push_back(real.substr(_root_dir_sz1)) ;
				} else {
					if      ( size_t i=_find_src_idx(real) ; i==Npos ) continue ;
					else if (                                +nxt    ) lnks.push_back( _env->src_dirs_s[i] + (real.c_str()+_abs_src_dirs_s[i].size()) ) ; // real lie in a source dir
				}
			}
			if (!nxt) {
				if (errno==ENOENT) exists = false ;
				// do not generate dep for intermediate dir that are not links as we indirectly depend on them through the last components
				// for example if a/b/c is a link to d/e and we access a/b/c/f, we generate the link a/b/c :
				// - a & a/b will be indirectly depended on through a/b/c
				// - d & d/e will be indrectly depended on through caller depending on d/e/f (the real accessed file returned as the result)
				continue ;
			}
			if (n_lnks++>=s_n_max_lnks) return {{},::move(lnks)} ; // link loop detected, same check as system
			if (!last) {                                           // append unprocessed part
				nxt.push_back('/'       ) ;
				nxt.append   (*cur,end+1) ;                        // avoiding this copy is very complex (would require to manage a stack) and links to dir are uncommon
			}
			if (nxt[0]=='/') { end =  0 ; prev_real_size = 0 ; }   // absolute link target : flush real
			else               end = -1 ;                          // end must point to the /, invent a virtual one before the string
			real.resize(prev_real_size) ;                          // links are relative to containing dir, suppress last component
			cur = &nxt ;
		}
		// admin is typically in repo, tmp might be, repo root is in_repo
		if (is_in_tmp) {
			if (!has_tmp_view                                                                           ) return { ::move(real) , ::move(lnks) , No    , FileLoc::Tmp     , false  } ;
			real = _env->tmp_dir + real.substr(_tmp_view->size()) ;
			/**/                                                                                          return { ::move(real) , ::move(lnks) , No    , FileLoc::Tmp     , true   } ;
		}
		if (+in_proc) {
			/**/                                                                                          return { ::move(real) , ::move(lnks) , No    , FileLoc::Proc    , mapped } ;
		}
		if (+in_admin) {
			/**/                                                                                          return { ::move(real) , ::move(lnks) , No    , FileLoc::Admin   , mapped } ;
		}
		if (+in_repo) {
			if (real.size()<_root_dir_sz1                                                               ) return { ::move(real) , ::move(lnks) , No    , FileLoc::Root    , mapped } ;
			real = real.substr(_root_dir_sz1) ;
			if ( _env->lnk_support>=LnkSupport::File && !no_follow                                      ) return { ::move(real) , ::move(lnks) , Yes   , FileLoc::Repo    , mapped } ;
			if ( _env->lnk_support>=LnkSupport::Full && real.find('/')!=Npos                            ) return { ::move(real) , ::move(lnks) , Maybe , FileLoc::Repo    , mapped } ;
			/**/                                                                                          return { ::move(real) , ::move(lnks) , No    , FileLoc::Repo    , mapped } ;
		}
		if ( size_t i=_find_src_idx(real) ; i!=Npos ) {
			real = _env->src_dirs_s[i] + (real.c_str()+_abs_src_dirs_s[i].size()) ;
			if ( _env->lnk_support>=LnkSupport::File && !no_follow                                      ) return { ::move(real) , ::move(lnks) , Yes   , FileLoc::SrcDirs , mapped } ;
			if ( _env->lnk_support>=LnkSupport::Full && real.find('/',_env->src_dirs_s[i].size())!=Npos ) return { ::move(real) , ::move(lnks) , Maybe , FileLoc::SrcDirs , mapped } ;
			/**/                                                                                          return { ::move(real) , ::move(lnks) , No    , FileLoc::SrcDirs , mapped } ;
		}
		{
			/**/                                                                                          return { ::move(real) , ::move(lnks) , No    , FileLoc::Ext     , mapped } ;
		}
	}

	::vmap_s<Accesses> RealPath::exec(SolveReport& sr) {
		::vmap_s<Accesses> res         ;
		::string           interpreter ; interpreter.reserve(256) ;
		::string           root_dir_s  = _env->root_dir+'/'       ;
		// from tmp, we can go back to repo
		for( int i=0 ; i<=4 ; i++ ) {                                                             // interpret #!<interpreter> recursively (4 levels as per man execve)
			for( ::string& l : sr.lnks ) res.emplace_back(::move(l),Accesses(Access::Lnk)) ;
			//
			if (sr.file_loc>FileLoc::Dep && sr.file_loc!=FileLoc::Tmp) break ;                    // if we escaped from the repo, there is no more deps to gather
			//
			if (sr.mapped)
				throw to_string("executing ",mk_file(sr.real)," with mapped files along its interpreter path from ",_tmp_view," to ",_env->tmp_dir," would require to modify file contents") ;
			//
			::ifstream real_stream { mk_abs(sr.real,root_dir_s) } ;
			Accesses   a           = Access::Reg                  ; if (sr.file_accessed==Yes) a |= Access::Lnk ;
			if (sr.file_loc<=FileLoc::Dep) res.emplace_back(sr.real,a) ;
			//
			char hdr[2] ;
			if (!real_stream.read(hdr,sizeof(hdr))) break ;
			if (strncmp(hdr,"#!",2)!=0            ) break ;
			interpreter.resize(256) ;
			real_stream.getline(interpreter.data(),interpreter.size()) ;                          // man execve specifies that data beyond 255 chars are ignored
			if (!real_stream.gcount()) break ;
			interpreter.resize(real_stream.gcount()) ;
			if      ( size_t pos = interpreter.find(' ' ) ; pos!=Npos ) interpreter.resize(pos) ; // interpreter is the first word
			else if ( size_t pos = interpreter.find('\0') ; pos!=Npos ) interpreter.resize(pos) ; // interpreter is the entire line (or the first \0 delimited word)
			// recurse
			sr = solve(interpreter,false/*no_follow*/) ;
		}
		return res ;
	}

}
