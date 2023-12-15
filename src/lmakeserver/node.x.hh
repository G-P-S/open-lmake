// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

// included 3 times : with DEF_STRUCT defined, then with DATA_DEF defined, then with IMPL defined

#include "rpc_job.hh"

#ifdef STRUCT_DECL
namespace Engine {

	struct Node        ;
	struct NodeData    ;
	struct NodeReqInfo ;

	struct Target  ;
	using Targets = TargetsBase ;


	struct Dep  ;
	struct Deps ;

	static constexpr uint8_t NodeNGuardBits = 1 ;                              // to be able to make Target

	ENUM( Buildable
	,	LongName                       //                                   name is longer than allowed in config
	,	DynAnti                        //                                   match dependent
	,	Anti                           //                                   match independent
	,	SrcDir                         //                                   match independent (much like star targets, i.e. only existing files are deemed buildable)
	,	No                             // <=No means node is not buildable
	,	Maybe                          //                                   buildability is data dependent (maybe converted to Yes by further analysis)
	,	SubSrcDir                      //                                   sub-file of a src dir listed in manifest
	,	Yes                            // >=Yes means node is buildable
	,	DynSrc                         //                                   match dependent
	,	Src                            //                                   match independent
	,	SubSrc                         //                                   sub-file of a src listed in manifest
	,	Loop                           //                                   node is being analyzed, deemed buildable so as to block further analysis
	)

	ENUM_1( NodeMakeAction
	,	Dec = Wakeup                   // >=Dec means n_wait must be decremented
	,	None
	,	Wakeup                         // a job has completed
	)

	ENUM_1( NodeStatus
	,	Makable = Src                  // <=Makable means node can be used as dep
	,	Plain                          // must be first (as 0 is deemed to be a job_tgt index), node is generated by a job
	,	Multi                          // several jobs
	,	Src                            // node is a src     or a file within a src dir
	,	SrcDir                         // node is a src dir or a dir  within a src dir
	,	None                           // no job
	,	Transcient                     // node has a link         as uphill dir (and such a dep will certainly disappear when job is remade unless it is a static dep)
	,	Uphill                         // node has a regular file as uphill dir
	)

}
#endif
#ifdef STRUCT_DEF
namespace Engine {

	//
	// Node
	//

	struct Node : NodeBase {
		friend ::ostream& operator<<( ::ostream& , Node const ) ;
		using MakeAction = NodeMakeAction ;
		using ReqInfo    = NodeReqInfo    ;
		//
		static constexpr RuleIdx NoIdx      = -1                  ;
		static constexpr RuleIdx MaxRuleIdx = -(+NodeStatus::N+1) ;
		//
		using NodeBase::NodeBase ;
	} ;

	//
	// Target
	//

	struct Target : Node {
		static_assert(Node::NGuardBits>=1) ;
		static constexpr uint8_t NGuardBits = Node::NGuardBits-1      ;
		static constexpr uint8_t NValBits   = NBits<Idx> - NGuardBits ;
		friend ::ostream& operator<<( ::ostream& , Target const ) ;
		// cxtors & casts
		Target(                        ) = default ;
		Target( Node n , bool iu=false ) : Node(n) { is_unexpected( +n && iu        ) ; } // if no node, ensure Target appears as false
		Target( Target const& t        ) : Node(t) { is_unexpected(t.is_unexpected()) ; }
		//
		Target& operator=(Target const& tu) { Node::operator=(tu) ; is_unexpected(tu.is_unexpected()) ; return *this ; }
		// accesses
		Idx operator+() const { return Node::operator+() | is_unexpected()<<(NValBits-1) ; }
		//
		template<uint8_t W,uint8_t LSB=0> requires( W>0 && W+LSB<=NGuardBits ) Idx  side(       ) const = delete ; // { return Node::side<W,LSB+1>(   ) ; }
		template<uint8_t W,uint8_t LSB=0> requires( W>0 && W+LSB<=NGuardBits ) void side(Idx val)       = delete ; // {        Node::side<W,LSB+1>(val) ; }
		//
		bool is_unexpected(        ) const { return Node::side<1>(   ) ; }
		void is_unexpected(bool val)       {        Node::side<1>(val) ; }
		// services
		bool lazy_tflag( Tflag tf , Rule::SimpleMatch const& sm , Rule::FullMatch& fm , ::string& tn ) ; // fm & tn are lazy evaluated
	} ;

	//
	// Dep
	//

	struct Dep : DepDigestBase<Node> {
		friend ::ostream& operator<<( ::ostream& , Dep const& ) ;
		using Base = DepDigestBase<Node> ;
		// cxtors & casts
		using Base::Base ;
		// accesses
		::string accesses_str() const ;
		::string dflags_str  () const ;
		// services
		bool up_to_date () const ;
		void acquire_crc() ;
	} ;
	static_assert(sizeof(Dep)==16) ;

	//
	// Deps
	//

	struct Deps : DepsBase {
		friend ::ostream& operator<<( ::ostream& , Deps const& ) ;
		// cxtors & casts
		using DepsBase::DepsBase ;
		Deps( ::vmap  <Node,AccDflags> const& ,                                  bool parallel=true ) ;
		Deps( ::vmap  <Node,Dflags   > const& , Accesses ,                       bool parallel=true ) ;
		Deps( ::vector<Node          > const& , Accesses , Dflags=StaticDflags , bool parallel=true ) ;
	} ;

}
#endif
#ifdef INFO_DEF
namespace Engine {

	ENUM( NodeLvl
	,	None       // reserve value 0 as they are not counted in n_total
	,	Zombie     // req is zombie but node not marked done yet
	,	Uphill     // first level set at init, uphill directory
	,	NoJob      // job candidates are exhausted
	,	Plain      // >=PlainLvl means plain jobs starting at lvl-Lvl::Plain (all at same priority)
	)

	struct NodeReqInfo : ReqInfo {                                              // watchers of Node's are Job's
		friend ::ostream& operator<<( ::ostream& os , NodeReqInfo const& ri ) ;
		//
		using Lvl        = NodeLvl        ;
		using MakeAction = NodeMakeAction ;
		//
		static constexpr RuleIdx NoIdx = Node::NoIdx ;
		static const     ReqInfo Src   ;
		// cxtors & casts
		using ReqInfo::ReqInfo ;
		// services
		void update( RunAction , MakeAction , NodeData const& ) ;
		// data
	public :
		RuleIdx prio_idx    = NoIdx ;  //    16 bits
		bool    single      = false ;  // 1<= 8 bits, if true <=> consider only job indexed by prio_idx, not all jobs at this priority
		bool    overwritten = false ;  // 1<= 8 bits
	} ;
	static_assert(sizeof(NodeReqInfo)==24) ;                                   // check expected size

}
#endif
#ifdef DATA_DEF
namespace Engine {

	struct NodeData : DataBase {
		using Idx        = NodeIdx        ;
		using ReqInfo    = NodeReqInfo    ;
		using MakeAction = NodeMakeAction ;
		using LvlIdx     = RuleIdx        ;                                    // lvl may indicate the number of rules tried
		//
		static constexpr RuleIdx MaxRuleIdx = Node::MaxRuleIdx ;
		static constexpr RuleIdx NoIdx      = Node::NoIdx      ;
		//
		// cxtors & casts
		NodeData() = default ;
		NodeData( Name n , bool no_dir ) : DataBase{n} {
			if (!no_dir) dir = dir_name() ;
		}
		~NodeData() {
			job_tgts.pop() ;
		}
		// accesses
		Node     idx    () const { return Node::s_idx(*this) ; }
		::string name   () const { return full_name()        ; }
		size_t   name_sz() const { return full_name_sz()     ; }
		//
		bool           has_req   (Req           ) const ;
		ReqInfo const& c_req_info(Req           ) const ;
		ReqInfo      & req_info  (Req           ) const ;
		ReqInfo      & req_info  (ReqInfo const&) const ;                      // make R/W while avoiding look up (unless allocation)
		::vector<Req>  reqs      (              ) const ;
		bool           waiting   (              ) const ;
		bool           done      (ReqInfo const&) const ;
		bool           done      (Req           ) const ;
		//
		bool     match_ok          (         ) const {                          return match_gen>=Rule::s_match_gen                   ; }
		bool     has_actual_job    (         ) const {                          return +actual_job_tgt && !actual_job_tgt->rule.old() ; }
		bool     has_actual_job    (Job    j ) const { SWEAR(!j ->rule.old()) ; return actual_job_tgt==j                              ; }
		bool     has_actual_job_tgt(JobTgt jt) const { SWEAR(!jt->rule.old()) ; return actual_job_tgt==jt                             ; }
		//
		Bool3 manual        ( Ddate                  ) const ;
		Bool3 manual_refresh( Req            , Ddate ) ;                                                            // refresh date if file was updated but steady
		Bool3 manual_refresh( JobData const& , Ddate ) ;                                                            // .
		Bool3 manual        (                        ) const { return manual        (  Disk::file_date(name())) ; }
		Bool3 manual_refresh( Req            r       )       { return manual_refresh(r,Disk::file_date(name())) ; }
		Bool3 manual_refresh( JobData const& j       )       { return manual_refresh(j,Disk::file_date(name())) ; }
		//
		RuleIdx    conform_idx(              ) const { if   (_conform_idx<=MaxRuleIdx)   return _conform_idx              ; else return NoIdx             ; }
		void       conform_idx(RuleIdx    idx)       { SWEAR(idx         <=MaxRuleIdx) ; _conform_idx = idx               ;                                 }
		NodeStatus status     (              ) const { if   (_conform_idx> MaxRuleIdx)   return NodeStatus(-_conform_idx) ; else return NodeStatus::Plain ; }
		void       status     (NodeStatus s  )       { SWEAR(+s                      ) ; _conform_idx = -+s               ;                                 }
		//
		JobTgt conform_job_tgt() const {
			if (status()==NodeStatus::Plain) return job_tgts[conform_idx()] ;
			else                             return {}                      ;
		}
		bool conform() const {
			JobTgt cjt = conform_job_tgt() ;
			return +cjt && ( cjt->is_special() || has_actual_job_tgt(cjt) ) ;
		}
		Bool3 ok() const {                                                     // Maybe means not built
			switch (status()) {
				case NodeStatus::Plain : return No    | !conform_job_tgt()->err() ;
				case NodeStatus::Multi : return No                                ;
				case NodeStatus::Src   : return Yes   & (crc!=Crc::None)          ;
				default                : return Maybe                             ;
			}
		}
		Bool3 ok    (ReqInfo const& cri) const { SWEAR(cri.done()) ; return cri.overwritten ? No : ok() ; }
		bool  is_src(                  ) const {
			SWEAR(match_ok()) ;
			switch (buildable) {
				case Buildable::LongName  :
				case Buildable::DynAnti   :
				case Buildable::Anti      :
				case Buildable::SrcDir    : return true  ;
				case Buildable::No        :
				case Buildable::Maybe     : return false ;
				case Buildable::SubSrcDir : return true  ;
				case Buildable::Yes       : return false ;
				case Buildable::DynSrc    :
				case Buildable::Src       :
				case Buildable::SubSrc    : return true  ;
				default : FAIL() ;
			}
		}
		//
		// services
		bool read(Accesses a) const {                                          // return true <= file was perceived different from non-existent, assuming access provided in a
			if (crc==Crc::None ) return false          ;                       // file does not exist, cannot perceive difference
			if (a[Access::Stat]) return true           ;                       // if file exists, stat is different
			if (crc.is_lnk()   ) return a[Access::Lnk] ;
			if (+crc           ) return a[Access::Reg] ;
			else                 return +a             ;                       // dont know if file is a link, any access may have perceived a difference
		}
		bool up_to_date(DepDigest const& dd) const { return crc.match(dd.crc(),dd.accesses) ; } // only manage crc, not dates
		//
		::vector<RuleTgt> raw_rule_tgts(         ) const ;
		void              mk_old       (         ) ;
		void              mk_src       (Buildable) ;
		void              mk_no_src    (         ) ;
		//
		::vector_view_c<JobTgt> prio_job_tgts   (RuleIdx prio_idx) const ;
		::vector_view_c<JobTgt> conform_job_tgts(ReqInfo const&  ) const ;
		::vector_view_c<JobTgt> conform_job_tgts(                ) const ;
		//
		void set_buildable( Req={}   , DepDepth lvl=0       ) ;                // data independent, may be pessimistic (Maybe instead of Yes), req is for error reporing only
		void set_pressure ( ReqInfo& , CoarseDelay pressure ) const ;
		//
		void set_infinite(::vector<Node> const& deps) ;
		//
		void make( ReqInfo& , RunAction=RunAction::Status , Watcher asking={} , MakeAction=MakeAction::None ) ;
		//
		void make( ReqInfo& ri , MakeAction ma ) { return make(ri,RunAction::Status,{}/*asking*/,ma) ; } // for wakeup
		//
		bool/*ok*/ forget( bool targets , bool deps ) ;
		//
		template<class RI> void add_watcher( ReqInfo& ri , Watcher watcher , RI& wri , CoarseDelay pressure ) ;
		//
		bool/*modified*/ refresh( Crc , Ddate ) ;
		void             refresh(             ) ;
	private :
		void         _set_buildable_raw( Req      , DepDepth                                                     ) ; // req is for error reporting only
		bool/*done*/ _make_pre         ( ReqInfo&                                                                ) ;
		void         _make_raw         ( ReqInfo& , RunAction , Watcher asking_={} , MakeAction=MakeAction::None ) ;
		void         _set_pressure_raw ( ReqInfo&                                                                ) const ;
		//
		Buildable _gather_special_rule_tgts( ::string const& name                          ) ;
		Buildable _gather_prio_job_tgts    ( ::string const& name , Req   , DepDepth lvl=0 ) ;
		Buildable _gather_prio_job_tgts    (                        Req r , DepDepth lvl=0 ) {
			if (rule_tgts.empty()) return Buildable::No                             ;          // fast path : avoid computing name()
			else                   return _gather_prio_job_tgts( name() , r , lvl ) ;
		}
		//
		void _set_match_gen(bool ok  ) ;
		void _set_buildable(Buildable) ;
		// data
	public :
		Watcher   asking                  ;                      //      32 bits,         last watcher needing this node
		Ddate     date                    ;                      // ~40<=64 bits,         deemed mtime (in ns) or when it was known non-existent. 40 bits : lifetime=30 years @ 1ms resolution
		Crc       crc                     = Crc::None          ; // ~45<=64 bits,         disk file CRC when file's mtime was date. 45 bits : MTBF=1000 years @ 1000 files generated per second.
		Node      dir                     ;                      //      31 bits, shared
		JobTgts   job_tgts                ;                      //      32 bits, owned , ordered by prio, valid if match_ok
		RuleTgts  rule_tgts               ;                      // ~20<=32 bits, shared, matching rule_tgts issued from suffix on top of job_tgts, valid if match_ok
		JobTgt    actual_job_tgt          ;                      //  31<=32 bits, shared, job that generated node
		MatchGen  match_gen:NMatchGenBits = 0                  ; //       8 bits,         if <Rule::s_match_gen => deem !job_tgts.size() && !rule_tgts && !sure
		Buildable buildable:4             = Buildable::Unknown ; //       4 bits,         data independent, if Maybe => buildability is data dependent, if Unknown => not yet computed
		bool      unlinked :1             = false              ; //       1 bit ,         if true <=> node as been unlinked by another rule
	private :
		RuleIdx   _conform_idx = -+NodeStatus::Unknown         ; //      16 bits,         index to job_tgts to first job with execut.ing.ed prio level, if NoIdx <=> uphill or no job found
	} ;
	static_assert(sizeof(NodeData)==48) ;                                      // check expected size

}
#endif
#ifdef IMPL
namespace Engine {

	//
	// NodeReqInfo
	//

	inline void NodeReqInfo::update( RunAction run_action , MakeAction make_action , NodeData const& node ) {
		if (make_action>=MakeAction::Dec) {
			SWEAR(n_wait) ;
			n_wait-- ;
		}
		if (run_action>action) {                                               // normally, increasing action requires to reset checks
			action = run_action ;
			if (action!=RunAction::Dsk) prio_idx = NoIdx ;                     // except transition Dsk->Run which is no-op for Node
		}
		if (n_wait) return ;
		if      ( req->zombie                                                  ) done_ = RunAction::Dsk     ;
		else if ( node.buildable>=Buildable::Yes && action==RunAction::Makable ) done_ = RunAction::Makable ;
	}

	//
	// NodeData
	//

	inline bool NodeData::has_req(Req r) const {
		return Req::s_store[+r].nodes.contains(idx()) ;
	}
	inline Node::ReqInfo const& NodeData::c_req_info(Req r) const {
		::umap<Node,ReqInfo> const& req_infos = Req::s_store[+r].nodes ;
		auto                        it        = req_infos.find(idx())  ;       // avoid double look up
		if (it==req_infos.end()) return Req::s_store[+r].nodes.dflt ;
		else                     return it->second                  ;
	}
	inline Node::ReqInfo& NodeData::req_info(Req r) const {
		return Req::s_store[+r].nodes.try_emplace(idx(),NodeReqInfo(r)).first->second ;
	}
	inline Node::ReqInfo& NodeData::req_info(ReqInfo const& cri) const {
		if (&cri==&Req::s_store[+cri.req].nodes.dflt) return req_info(cri.req)         ; // allocate
		else                                          return const_cast<ReqInfo&>(cri) ; // already allocated, no look up
	}
	inline ::vector<Req> NodeData::reqs() const { return Req::reqs(*this) ; }

	inline bool NodeData::waiting() const {
		for( Req r : reqs() ) if (c_req_info(r).waiting()) return true ;
		return false ;
	}

	inline bool NodeData::done( ReqInfo const& cri ) const { return cri.done(cri.action) || buildable<=Buildable::No ; }
	inline bool NodeData::done( Req            r   ) const { return done(c_req_info(r))                              ; }

	inline Bool3 NodeData::manual(Ddate d) const {
		const char* res_str ;
		Bool3       res     ;
		if      (crc==Crc::None) { if (!d) return No ; res_str = "created"     ; res = Yes   ; }
		else if (!d            ) {                     res_str = "disappeared" ; res = Maybe ; }
		else if (d==date       ) {         return No ;                                         }
		else                     {                     res_str = "newer"       ; res = Yes   ; }
		//
		Trace("manual",idx(),d,date,crc,date,res_str) ;
		return res ;
	}

	inline ::vector_view_c<JobTgt> NodeData::conform_job_tgts(ReqInfo const& cri) const { return prio_job_tgts(cri.prio_idx) ; }
	inline ::vector_view_c<JobTgt> NodeData::conform_job_tgts(                  ) const {
		// conform_idx is (one of) the producing job, not necessarily the first of the job_tgt's at same prio level
		if (status()!=NodeStatus::Plain) return {} ;
		RuleIdx prio_idx = conform_idx() ;
		Prio prio = job_tgts[prio_idx]->rule->prio ;
		while ( prio_idx && job_tgts[prio_idx-1]->rule->prio==prio ) prio_idx-- ; // rewind to first job within prio level
		return prio_job_tgts(prio_idx) ;
	}

	inline ::vector<RuleTgt> NodeData::raw_rule_tgts() const {
		::vector<RuleTgt> rts = Node::s_rule_tgts(name()).view() ;
		::vector<RuleTgt> res ; res.reserve(rts.size())    ;                   // pessimistic reserve ensures no realloc
		Py::Gil           gil ;
		for( RuleTgt const& rt : rts )
			if (+rt.pattern().match(name())) res.push_back(rt) ;
		return res ;
	}

	template<class RI> inline void NodeData::add_watcher( ReqInfo& ri , Watcher watcher , RI& wri , CoarseDelay pressure ) {
		ri.add_watcher(watcher,wri) ;
		set_pressure(ri,pressure) ;
	}

	inline void NodeData::_set_match_gen(bool ok) {
		if      (!ok                        ) { buildable = Buildable::Unknown       ; match_gen = 0                 ; }
		else if (match_gen<Rule::s_match_gen) { SWEAR(buildable!=Buildable::Unknown) ; match_gen = Rule::s_match_gen ; }
	}

	inline void NodeData::_set_buildable(Buildable b) {
		SWEAR(b!=Buildable::Unknown) ;
		buildable = b ;
	}

	inline void NodeData::set_buildable( Req req , DepDepth lvl ) {            // req is for error reporting only
		if (match_ok()) return ;                                               // already set
		_set_buildable_raw(req,lvl) ;
	}

	inline void NodeData::set_pressure( ReqInfo& ri , CoarseDelay pressure ) const {
		if (!ri.set_pressure(pressure)) return ;                                     // pressure is not significantly higher than already existing, nothing to propagate
		if (!ri.waiting()             ) return ;
		_set_pressure_raw(ri) ;
	}

	inline void NodeData::make( ReqInfo& ri , RunAction run_action , Watcher asking , MakeAction make_action ) {
		// /!\ do not recognize buildable==No : we must execute set_buildable before in case a non-buildable becomes buildable
		if ( ri.done(run_action) && !(run_action>=RunAction::Dsk&&unlinked) && make_action<MakeAction::Dec ) return ;
		_make_raw(ri,run_action,asking,make_action) ;
	}

	inline void NodeData::refresh() {
		Ddate d = Disk::file_date(name()) ;
		switch (manual(d)) {
			case Yes   : refresh( {}        , d              ) ; break ;
			case Maybe : refresh( Crc::None , Ddate::s_now() ) ; break ;
			case No    :                                         break ;
			default : FAIL(d) ;
		}
	}

	//
	// Target
	//

	inline bool Target::lazy_tflag( Tflag tf , Rule::SimpleMatch const& sm , Rule::FullMatch& fm , ::string& tn ) { // fm & tn are lazy evaluated
		Bool3 res = sm.rule->common_tflags(tf,is_unexpected()) ;
		if (res!=Maybe) return res==Yes ;                                      // fast path : flag is common, no need to solve lazy evaluation
		if (!fm       ) fm = sm              ;                                 // solve lazy evaluation
		if (tn.empty()) tn = (*this)->name() ;                                 // .
		/**/            return sm.rule->tflags(fm.idx(tn))[tf] ;
	}

	//
	// Deps
	//

	inline Deps::Deps( ::vmap<Node,AccDflags> const& static_deps , bool p ) {
		::vector<Dep> ds ; ds.reserve(static_deps.size()) ;
		for( auto const& [d,af] : static_deps ) ds.emplace_back( d , af.accesses , af.dflags , p ) ;
		*this = Deps(ds) ;
	}

	inline Deps::Deps( ::vmap<Node,Dflags> const& static_deps , Accesses a , bool p ) {
		::vector<Dep> ds ; ds.reserve(static_deps.size()) ;
		for( auto const& [d,df] : static_deps ) { ds.emplace_back( d , a , df , p ) ; }
		*this = Deps(ds) ;
	}

	inline Deps::Deps( ::vector<Node> const& deps , Accesses a , Dflags df , bool p ) {
		::vector<Dep> ds ; ds.reserve(deps.size()) ;
		for( auto const& d : deps ) ds.emplace_back( d , a , df , p ) ;
		*this = Deps(ds) ;
	}

	//
	// Dep
	//

	inline bool Dep::up_to_date() const {
		return !is_date && crc().match((*this)->crc,accesses) ;
	}

	inline void Dep::acquire_crc() {
		if (!is_date             ) {                  return ; }               // no need
		if (!date()              ) { crc(Crc::None) ; return ; }               // no date means access did not find a file, crc is None, easy
		if (date()> (*this)->date) {                  return ; }               // file is manual, maybe too early and crc is not updated yet (also works if !(*this)->date)
		if (date()!=(*this)->date) { crc({}       ) ; return ; }               // too late, file has changed
		if (!(*this)->crc        ) {                  return ; }               // too early, no crc available yet
		crc((*this)->crc) ;                                                    // got it !
	}

}
#endif
