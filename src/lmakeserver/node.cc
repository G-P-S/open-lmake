// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "disk.hh"
#include "time.hh"

#include "core.hh"

namespace Engine {
	using namespace Disk ;
	using namespace Time ;

	//
	// NodeReqInfo
	//

	::ostream& operator<<( ::ostream& os , NodeReqInfo const& ri ) {
		/**/                          os << "NRI(" << ri.req <<','<< ri.action <<',' ;
		if (ri.prio_idx==Node::NoIdx) os << "None"                                   ;
		else                          os <<           ri.prio_idx                    ;
		if (+ri.done_               ) os <<",Done@"<< ri.done_                       ;
		if (ri.n_wait               ) os <<",wait:"<< ri.n_wait                      ;
		if (ri.overwritten          ) os <<",overwritten"                            ;
		return                        os <<')'                                       ;
	}

	//
	// Node
	//

	::ostream& operator<<( ::ostream& os , Node const n ) {
		/**/    os << "N(" ;
		if (+n) os << +n   ;
		return  os << ')'  ;
	}

	//
	// NodeData
	//

	::ostream& operator<<( ::ostream& os , NodeData const& nd ) {
		/**/                             os << '(' << nd.crc           ;
		/**/                             os << ',' << nd.date          ;
		/**/                             os << ','                     ;
		if (!nd.match_ok()             ) os << '~'                     ;
		/**/                             os << "job:"                  ;
		/**/                             os << +Job(nd.actual_job_tgt) ;
		if (nd.actual_job_tgt.is_sure()) os << '+'                     ;
		return                           os << ")"                     ;
	}

	void NodeData::_set_pressure_raw( ReqInfo& ri ) const {
		for( Job job : conform_job_tgts(ri) ) job->set_pressure(job->req_info(ri.req),ri.pressure) ; // go through current analysis level as this is where we may have deps we are waiting for
	}

	void NodeData::set_infinite(::vector<Node> const& deps) {
		Trace trace("set_infinite",idx(),deps) ;
		job_tgts.assign(::vector<JobTgt>({{
			Job( Special::Infinite , idx() , Deps(deps,{}/*accesses*/,{}/*dflags*/,false/*parallel*/) )
		,	true/*is_sure*/
		}})) ;
		Buildable buildable = Buildable::Yes ;
		for( Node const& d : deps )
			if (d->buildable==Buildable::Unknown) buildable &= Buildable::Maybe ; // if not computed yet, well note we do not know
			else                                  buildable &= d->buildable     ; // could break as soon as !Yes is seen, but this way, we can have a more agressive swear
		SWEAR(buildable>Buildable::No) ;
		if (buildable>=Buildable::Yes) rule_tgts.clear() ;
		_set_buildable(buildable) ;
	}

	::vector_view_c<JobTgt> NodeData::prio_job_tgts(RuleIdx prio_idx) const {
		if (prio_idx==NoIdx) return {} ;
		JobTgts const& jts = job_tgts ;                                        // /!\ jts is a CrunchVector, so if single element, a subvec would point to it, so it *must* be a ref
		if (prio_idx>=jts.size()) {
			SWEAR( prio_idx==jts.size() , prio_idx , jts.size() ) ;
			return {} ;
		}
		RuleIdx                 sz   = 0                    ;
		::vector_view_c<JobTgt> sjts = jts.subvec(prio_idx) ;
		Prio                    prio = -Infinity            ;
		for( JobTgt jt : sjts ) {
			Prio new_prio = jt->rule->prio ;
			if (new_prio<prio) break ;
			prio = new_prio ;
			sz++ ;
		}
		return sjts.subvec(0,sz) ;
	}

	struct JobTgtIter {
		// cxtors
		JobTgtIter( NodeData& n , NodeReqInfo const& ri ) : node{n} , idx{ri.prio_idx} , single{ri.single} {}
		// services
		JobTgtIter& operator++(int) {
			_prev_prio = _cur_prio() ;
			if (single) idx = node.job_tgts.size() ;
			else        idx++                      ;
			return *this ;
		}
		JobTgt        operator* (   ) const { return node.job_tgts[idx] ;                                  }
		JobTgt const* operator->(   ) const { return node.job_tgts.begin()+idx ;                           }
		JobTgt      * operator->(   )       { return node.job_tgts.begin()+idx ;                           }
		operator bool           (   ) const { return idx<node.job_tgts.size() && _cur_prio()>=_prev_prio ; }
		void reset(RuleIdx i=0) {
			idx        = i         ;
			_prev_prio = -Infinity ;
		}
	private :
		Prio _cur_prio() const { return (**this)->rule->prio ; }
		// data
	public :
		NodeData& node   ;
		RuleIdx   idx    = 0    /*garbage*/ ;
		bool      single = false/*garbage*/ ;
	private :
		Prio _prev_prio = -Infinity ;
	} ;

	// check rule_tgts special rules and set rule_tgts accordingly
	Buildable NodeData::_gather_special_rule_tgts(::string const& name_) {
		RuleIdx           n          = 0               ;
		::vector<RuleTgt> rule_tgts_ = raw_rule_tgts() ;
		job_tgts.clear() ;
		for( RuleTgt const& rt : rule_tgts_ ) {
			if (!rt->is_special()) {
				rule_tgts = ::vector_view_c<RuleTgt>(rule_tgts_,n) ;
				return Buildable::Maybe ;
			}
			if (+Rule::FullMatch(rt,name_))
				switch (rt->special) {
					case Special::GenericSrc : rule_tgts = ::vector<RuleTgt>({rt}) ; return Buildable::DynSrc  ;
					case Special::Anti       : rule_tgts = ::vector<RuleTgt>({rt}) ; return Buildable::DynAnti ;
					default : FAIL(rt->special) ;
				}
			n++ ;
		}
		rule_tgts.clear() ;
		return Buildable::Maybe ;                                              // node may be buildable from dir
	}

	// instantiate rule_tgts into job_tgts by taking the first iso-prio chunk and set rule_tgts accordingly
	// - special rules (always first) are already processed
	// - if a sure job is found, then all rule_tgts are consumed as there will be no further match
	Buildable NodeData::_gather_prio_job_tgts( ::string const& name_ , Req req , DepDepth lvl ) {
		//
		Prio              prio       = -Infinity        ;                      // initially, we are ready to accept any rule
		RuleIdx           n          = 0                ;
		Buildable         buildable  = Buildable::No    ;                      // return val if we find no job candidate
		::vector<RuleTgt> rule_tgts_ = rule_tgts.view() ;
		//
		SWEAR(is_lcl(name_)) ;
		::vector<JobTgt> jts ; jts.reserve(rule_tgts_.size()) ;                // typically, there is a single priority
		for( RuleTgt const& rt : rule_tgts_ ) {
			SWEAR(!rt->is_special()) ;
			if (rt->prio<prio) goto Done ;
			//          vvvvvvvvvvvvvvvvvvvvvvvvvv
			JobTgt jt = JobTgt(rt,name_,req,lvl+1) ;
			//          ^^^^^^^^^^^^^^^^^^^^^^^^^^
			if (+jt) {
				if (jt.sure()) { buildable  = Buildable::Yes   ; n = NoIdx ; } // after a sure job, we can forget about rules at lower prio
				else             buildable |= Buildable::Maybe ;
				jts.push_back(jt) ;
				prio = rt->prio ;
			}
			if (n!=NoIdx) n++ ;
		}
		n = NoIdx ;                                                            // we have exhausted all rules
	Done :
		//            vvvvvvvvvvvvvvvvvvvvvvvvv
		if (+jts    ) job_tgts .append    (jts) ;
		if (n==NoIdx) rule_tgts.clear     (   ) ;
		else          rule_tgts.shorten_by(n  ) ;
		//            ^^^^^^^^^^^^^^^^^^^^^^^^^
		return buildable ;
	}

	void NodeData::_set_buildable_raw( Req req , DepDepth lvl ) {
		Trace trace("set_buildable",idx(),lvl) ;
		switch (buildable) {                                                   // ensure we do no update sources
			case Buildable::Src    :
			case Buildable::SrcDir :
			case Buildable::Anti   : SWEAR(!rule_tgts,rule_tgts) ; goto Return ;
			default : ;
		}
		status(NodeStatus::Unknown) ;
		//
		{	::string name_ = name() ;
			//
			{	Buildable buildable = _gather_special_rule_tgts(name_) ;
				//                                    vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
				if (buildable<=Buildable::No      ) { _set_buildable(Buildable::No      ) ; goto Return ; } // AntiRule have priority so no warning message is generated
				if (name_.size()>g_config.path_max) { _set_buildable(Buildable::LongName) ; goto Return ; } // path is ridiculously long, make it unbuildable
				if (buildable>=Buildable::Yes     ) { _set_buildable(buildable          ) ; goto Return ; }
				//                                    ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
			}
			_set_buildable(Buildable::Loop) ;                                  // during analysis, temporarily set buildable to break loops that will be caught at exec time ...
			try {                                                              // ... in case of crash, rescue mode is used and ensures all matches are recomputed
				Buildable db = Buildable::No ;
				if (+dir) {
					if (lvl>=g_config.max_dep_depth) throw ::vector<Node>() ;  // infinite dep path
					//vvvvvvvvvvvvvvvvvvvvvvvvvvv
					dir->set_buildable(req,lvl+1) ;
					//^^^^^^^^^^^^^^^^^^^^^^^^^^^
					switch ((db=dir->buildable)) {
						case Buildable::DynAnti   :
						case Buildable::Anti      :
						case Buildable::No        :
						case Buildable::Maybe     :                                        break       ;
						//                          vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
						case Buildable::Yes       : _set_buildable(Buildable::Yes      ) ; goto Return ;
						case Buildable::DynSrc    :
						case Buildable::Src       :
						case Buildable::SubSrc    : _set_buildable(Buildable::SubSrc   ) ; goto Return ;
						case Buildable::SrcDir    :
						case Buildable::SubSrcDir : _set_buildable(Buildable::SubSrcDir) ; goto Return ;
						//                          ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
						default : FAIL(dir->buildable) ;
					}
				}
				if (!is_lcl(name_)) { _set_buildable(Buildable::No) ; goto Return ; }
				//
				Buildable buildable = _gather_prio_job_tgts(name_,req,lvl) ;
				if (db==Buildable::Maybe) buildable |= Buildable::Maybe ;                       // we are at least as buildable as our dir
				//vvvvvvvvvvvvvvvvvvvvvvv
				_set_buildable(buildable) ;
				//^^^^^^^^^^^^^^^^^^^^^^^
				goto Return ;
			} catch (::vector<Node>& e) {
				_set_match_gen(false/*ok*/) ;                                  // restore Unknown as we do not want to appear as having been analyzed
				e.push_back(idx()) ;
				throw ;
			}
		}
	Return :
		_set_match_gen(true/*ok*/) ;
		trace("done",buildable) ;
		return ;
	}

	bool/*done*/ NodeData::_make_pre( ReqInfo& ri ) {
		Trace trace("Nmake_pre",idx(),ri) ;
		Req      req   = ri.req ;
		::string name_ ;                                                       // lazy evaluated
		auto lazy_name = [&]()->::string const& {
			if (!name_) name_ = name() ;
			return name_ ;
		} ;
		// step 1 : handle what can be done without dir
		switch (buildable) {
			case Buildable::LongName :
				if (req->long_names.try_emplace(idx(),req->long_names.size()).second) { // if inserted
					size_t sz = lazy_name().size() ;
					SWEAR( sz>g_config.path_max , sz , g_config.path_max ) ;
					req->audit_node( Color::Warning , to_string("name is too long (",sz,'>',g_config.path_max,") for") , idx() ) ;
				}
				[[fallthrough]] ;
			case Buildable::DynAnti   :
			case Buildable::Anti      :
			case Buildable::SrcDir    :
			case Buildable::No        : status(NodeStatus::None) ; goto NoSrc ;
			case Buildable::DynSrc    :
			case Buildable::Src       : status(NodeStatus::Src ) ; goto Src   ;
			default                   :                            break      ;
		}
		if (!dir) goto NotDone ;
		// step 2 : handle what can be done without making dir
		switch (dir->buildable) {
			case Buildable::DynAnti :
			case Buildable::Anti    :
			case Buildable::No      :                            goto NotDone ;
			case Buildable::SrcDir  : status(NodeStatus::None) ; goto Src     ; // status is overwritten Src if node actually exists
			default                 :                            break        ;
		}
		if ( Node::ReqInfo& dri = dir->req_info(req) ; !dir->done(dri)) {      // fast path : no need to call make if dir is done
			if (!dri.waiting()) {
				ReqInfo::WaitInc sav_n_wait{ri} ;                              // appear waiting in case of recursion loop (loop will be caught because of no job on going)
				//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
				dir->make( dri , RunAction::Status , idx() ) ;
				//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
			}
			trace("dir",dir,STR(dir->done(dri)),ri) ;
			//
			if (dri.waiting()) {
				dir->add_watcher(dri,idx(),ri,ri.pressure) ;
				goto NotDone ;                                                 // return value is meaningless when waiting
			}
			SWEAR(dir->done(dri)) ;                                            // after having called make, dep must be either waiting or done
		}
		// step 3 : handle what needs dir status
		switch (dir->buildable) {
			case Buildable::Maybe :
				if (dir->status()==NodeStatus::None) goto NotDone ;
				[[fallthrough]] ;
			case Buildable::Yes :
				if (buildable==Buildable::Maybe) buildable = Buildable::Yes ; // propagate as dir->buildable may have changed from Maybe to Yes when made
				[[fallthrough]] ;
			case Buildable::SubSrc    :
			case Buildable::SubSrcDir :
				switch (dir->status()) {
					case NodeStatus::Transcient : status(NodeStatus::Transcient) ; goto NoSrc ; // forward
					case NodeStatus::Uphill     : status(NodeStatus::Uphill    ) ; goto NoSrc ; // .
					default : ;
				}
			break ;
			default : ;
		}
		// step 4 : handle what needs dir crc
		switch (dir->buildable) {
			case Buildable::Maybe     :
			case Buildable::Yes       :
			case Buildable::SubSrcDir :
				if (dir->crc==Crc::None) { status(NodeStatus::None) ; goto Src ; } // status is overwritten Src if node actually exists
				[[fallthrough]] ;
			case Buildable::DynSrc :
			case Buildable::Src    :
				if (dir->crc.is_lnk()) status(NodeStatus::Transcient) ;        // our dir is a link, we are transcient
				else                   status(NodeStatus::Uphill    ) ;        // a non-existent source stays a source, hence its sub-files are uphill
				goto NoSrc ;
			default : FAIL(dir->buildable) ;
		}
	Src :
		{	NfsGuard nfs_guard { g_config.reliable_dirs }        ;
			FileInfo fi        { nfs_guard.access(lazy_name()) } ;
			trace("src",status(),fi.date,date) ;
			if (!fi) {
				if (status()==NodeStatus::None) goto NoSrc ;                                             // if status was pre-set to None, it means we accept NoSrc
				req->audit_job( Color::Err , "missing" , idx().frozen()?"frozen":"src" , lazy_name() ) ;
				if (crc==Crc::None) goto Done ;
				refresh( Crc::None , Ddate::s_now() ) ;
			} else {
				status(NodeStatus::Src) ;                                      // overwrite status if it was pre-set to None
				if ( +crc && fi.date==date ) goto Done ;
				Crc crc_ { lazy_name() , g_config.hash_algo } ;
				SWEAR( +crc_ && crc_!=Crc::None ) ;
				bool new_   = crc==Crc::None  ;
				bool steady = crc_.match(crc) ;
				//vvvvvvvvvvvvvvvvvvvvvvv
				refresh( crc_ , fi.date ) ;
				//^^^^^^^^^^^^^^^^^^^^^^^
				if ( !steady && fi.date>req->start ) ri.overwritten = true ;
				::string step  = new_? "new" : steady ? "steady" : "changed" ;
				if (idx().frozen()) req->audit_job( Color::Warning  , step , "frozen" , lazy_name() ) ;
				else                req->audit_job( Color::HiddenOk , step , "src"    , lazy_name() ) ;
			}
			goto ActuallyDone ;
		}
	NoSrc :
		{	Crc crc_ = status()==NodeStatus::Transcient ? Crc::Unknown : Crc::None ;
			if (crc==crc_) goto Done ;                                                       // node is not polluted
			if ( ri.action>=RunAction::Dsk && crc_==Crc::None && manual_refresh(req)==No ) {
				unlink(lazy_name(),true/*dir_ok*/) ;                                         // wash pollution if not manual
				req->audit_job( Color::Warning , "wash" , "" , lazy_name() ) ;
			}
			refresh( crc_ , Ddate::s_now() ) ;                                 // if not physically unlinked, node will be manual
			goto ActuallyDone ;
		}
	ActuallyDone :
		actual_job_tgt.clear() ;
	Done :
		SWEAR(ri.done_<ri.action,ri.done_,ri.action) ;                         // else we should not be here
		ri.done_ = ri.action ;
	NotDone :
		trace("done",idx(),status(),crc,ri) ;
		return ri.done_>=ri.action ;
	}

	void NodeData::_make_raw( ReqInfo& ri , RunAction run_action , Watcher asking_ , MakeAction make_action ) {
		RuleIdx prod_idx   = NoIdx  ;
		Req     req        = ri.req ;
		Bool3   clean      = Maybe  ;                                          // lazy evaluate manual()==No
		bool    multi      = false  ;
		Trace trace("Nmake",idx(),ri,run_action,make_action) ;
		SWEAR(run_action<=RunAction::Dsk) ;
		if (+asking_) asking = asking_ ;
		//                                vvvvvvvvvvvvvvvvvv
		try                             { set_buildable(req) ; }
		//                                ^^^^^^^^^^^^^^^^^^
		catch (::vector<Node> const& e) { set_infinite(e)    ; }
		//
		ri.update( run_action , make_action , *this ) ;
		//
		if (ri.waiting()      ) goto Done ;
		if (ri.prio_idx==NoIdx) {
			if (ri.done(ri.action)) goto Wakeup ;
			//vvvvvvvvvvv
			_make_pre(ri) ;
			//^^^^^^^^^^^
			if (ri.waiting()      ) goto Done   ;
			if (ri.done(ri.action)) goto Wakeup ;
			ri.prio_idx = 0 ;
		} else {
			// check if we need to regenerate node
			if (ri.done(ri.action)) {
				if (!unlinked                   ) goto Wakeup ;                    // no need to regenerate
				if (ri.action<=RunAction::Status) goto Wakeup ;                    // no need for the file on disk
				if (status()!=NodeStatus::Plain ) goto Wakeup ;                    // no hope to regenerate, proceed as a done target
				ri.done_    = RunAction::Status ;                                  // regenerate
				ri.prio_idx = conform_idx()     ;                                  // .
				ri.single   = true              ;                                  // only ask to run conform job
				goto Make ;
			}
			// fast path : check jobs we were waiting for, lighter than full analysis
			JobTgtIter it{*this,ri} ;
			for(; it ; it++ ) {
				JobTgt jt   = *it                                                   ;
				bool   done = jt->c_req_info(req).done(ri.action&RunAction::Status) ;
				trace("check",jt,jt->c_req_info(req)) ;
				if (!done             ) { prod_idx = NoIdx ; goto Make ;                               } // we waited for it and it is not done, retry
				if (jt.produces(idx())) { if (prod_idx==NoIdx) prod_idx = it.idx ; else multi = true ; } // jobs in error are deemed to produce all their potential targets
			}
			if (prod_idx!=NoIdx) goto DoWakeup ;                               // we have at least one done job, no need to investigate any further
			ri.prio_idx = it.idx ;                                             // go on with next prio
		}
	Make :
		for ( Bool3 manual_ = unlinked ? Maybe : Bool3::Unknown ;;) {          // manual_ is lazy evaluated
			SWEAR(prod_idx==NoIdx,prod_idx) ;
			if (ri.prio_idx>=job_tgts.size()) {                                // gather new job_tgts from rule_tgts
				SWEAR(!ri.single) ;                                            // we only regenerate using an existing job
				try {
					//                      vvvvvvvvvvvvvvvvvvvvvvvvvv
					buildable = buildable | _gather_prio_job_tgts(req) ;
					//                      ^^^^^^^^^^^^^^^^^^^^^^^^^^
					if (ri.prio_idx>=job_tgts.size()) break ;                  // fast path
				} catch (::vector<Node> const& e) {
					set_infinite(e) ;
					break ;
				}
			}
			JobTgtIter it{*this,ri} ;
			if (!ri.single) {                                                  // fast path : cannot have several jobs if we consider only a single job
				for(; it ; it++ ) {                                            // check if we obviously have several jobs, in which case make nothing
					JobTgt jt = *it ;
					if      ( jt.sure()                 ) _set_buildable(Buildable::Yes) ; // buildable is data independent & pessimistic (may be Maybe instead of Yes)
					else if (!jt->c_req_info(req).done()) continue ;
					else if (!jt.produces(idx())        ) continue ;
					if (prod_idx!=NoIdx) { multi = true ; goto DoWakeup ; }
					prod_idx = it.idx ;
				}
				it.reset(ri.prio_idx) ;
				prod_idx = NoIdx ;
			}
			// make eligible jobs
			{	ReqInfo::WaitInc sav_n_wait{ri} ;                              // ensure we appear waiting while making jobs so loops will block (caught in Req::chk_end)
				for(; it ; it++ ) {
					JobTgt    jt     = *it               ;
					RunAction action = RunAction::Status ;
					JobReason reason ;
					switch (ri.action) {
						case RunAction::Makable : if (jt.is_sure()) action = RunAction::Makable ; break ; // if star, job must be run to know if we are generated
						case RunAction::Status  :                                                 break ;
						case RunAction::Dsk :
							if (jt.produces(idx())) {
								if      (!has_actual_job(  )) reason = {JobReasonTag::NoTarget     ,+idx()} ;
								else if (!has_actual_job(jt)) reason = {JobReasonTag::PolutedTarget,+idx()} ;
								else {
									if (manual_==Bool3::Unknown) manual_ = manual_refresh(req) ; // lazy solve manual_
									switch (manual_) {
										case Yes   : reason = {JobReasonTag::PolutedTarget,+idx()} ; break ;
										case Maybe : reason = {JobReasonTag::NoTarget     ,+idx()} ; break ;
										case No    :                                                 break ;
										default : FAIL(manual_) ;
									}
								}
							}
						break ;
						default : FAIL(ri.action) ;
					}
					Job::ReqInfo& jri = jt->req_info(req) ;
					if (ri.live_out) jri.live_out = ri.live_out ;              // transmit user request to job for last level live output
					//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
					jt->make( jri , action , reason , idx() ) ;
					//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
					trace("job",ri,clean,action,jt,STR(jri.waiting()),STR(jt.produces(idx()))) ;
					if      (jri.waiting()     ) jt->add_watcher(jri,idx(),ri,ri.pressure) ;
					else if (jt.produces(idx())) { if (prod_idx==NoIdx) prod_idx = it.idx ; else multi = true ; } // jobs in error are deemed to produce all their potential targets
				}
			}
			if (ri.waiting()   ) goto Done ;
			if (prod_idx!=NoIdx) break     ;
			ri.prio_idx = it.idx ;
		}
	DoWakeup :
		if      (prod_idx==NoIdx) status     (NodeStatus::None) ;
		else if (!multi         ) conform_idx(prod_idx        ) ;
		else {
			status(NodeStatus::Multi) ;
			::vector<JobTgt> jts ;
			for( JobTgt jt : conform_job_tgts(ri) ) if (jt.produces(idx())) jts.push_back(jt) ;
			trace("multi",ri,job_tgts.size(),conform_job_tgts(ri),jts) ;
			/**/                   req->audit_node(Color::Err ,"multi",idx()            ) ;
			/**/                   req->audit_info(Color::Note,"several rules match :",1) ;
			for( JobTgt jt : jts ) req->audit_info(Color::Note,jt->rule->name         ,2) ;
		}
		ri.done_ = ri.action ;
	Wakeup :
		SWEAR(done(ri)) ;
		trace("wakeup",ri,conform_idx()) ;
		ri.wakeup_watchers() ;
	Done :
		trace("done",ri) ;
	}

	bool/*ok*/ NodeData::forget( bool targets , bool deps ) {
		Trace trace("Nforget",idx(),STR(targets),STR(deps),STR(waiting()),conform_job_tgts()) ;
		if (waiting()) return false ;
		//
		bool    res  = true      ;
		RuleIdx k    = 0         ;
		Prio    prio = -Infinity ;
		for( Job j : job_tgts ) {
			if (j->rule->prio<prio) break ;                                    // all jobs above or besides conform job(s)
			res &= j->forget(targets,deps) ;
			if (k==conform_idx()) prio = j->rule->prio ;
			k++ ;
		}
		_set_match_gen(false/*ok*/) ;
		return res ;
	}

	void NodeData::mk_old() {
		Trace trace("mk_old",idx()) ;
		if ( +actual_job_tgt && actual_job_tgt->rule.old() ) actual_job_tgt.clear() ; // old jobs may be collected, do not refer to them anymore
		_set_match_gen(false/*ok*/) ;
	}

	void NodeData::mk_no_src() {
		Trace trace("mk_no_src",idx()) ;
		_set_match_gen(false/*ok*/) ;
		fence() ;
		rule_tgts     .clear() ;
		job_tgts      .clear() ;
		actual_job_tgt.clear() ;
		refresh() ;
	}

	void NodeData::mk_src(Buildable b) {
		Trace trace("mk_src",idx()) ;
		_set_buildable(b) ;
		fence() ;
		rule_tgts     .clear() ;
		_set_match_gen(true/*ok*/) ;
		job_tgts      .clear() ;
		actual_job_tgt.clear() ;
		refresh( Crc::None , {} ) ;
	}

	bool/*modified*/ NodeData::refresh( Crc crc_ , Ddate date_ ) {
		bool modified = !crc.match(crc_) ;
		Trace trace("refresh",idx(),STR(modified),crc,"->",crc_,date,"->",date_) ;
		if (modified) { crc = {} ; fence() ; date = date_ ; fence() ; crc = crc_ ; } // ensure crc is never associated with a wrong date
		else          {                      date = date_ ;                        } // regulars and links cannot have the same crc
		//
		if (unlinked) trace("!unlinked") ;
		unlinked = false ;                                                                             // dont care whether file exists, it has been generated according to its job
		if (modified) for( Req r : reqs() ) if (c_req_info(r).done()) req_info(r).overwritten = true ;
		return modified ;
	}

	static inline ::pair<Bool3,bool/*refreshed*/> _manual_refresh( NodeData& nd , Ddate d ) {
		Bool3 m = nd.manual(d) ;
		if (m!=Yes           ) return {m,false/*refreshed*/} ;                 // file was not modified
		if (nd.crc==Crc::None) return {m,false/*refreshed*/} ;                 // file appeared, it cannot be steady
		//
		::string nm = nd.name() ;
		Ddate ndd ;
		Crc   crc { ndd , nm , g_config.hash_algo } ;
		if (!nd.crc.match(crc)) return {Yes,false/*refreshed*/} ;              // real modif
		//
		nd.date = ndd ;
		return {No,true/*refreshed*/} ;                                        // file is steady
	}
	Bool3 NodeData::manual_refresh( Req req , Ddate d ) {
		auto [m,refreshed] = _manual_refresh(*this,d) ;
		if ( refreshed && +req ) req->audit_node(Color::Note,"manual_steady",idx()) ;
		return m ;
	}
	Bool3 NodeData::manual_refresh( JobData const& j , Ddate d ) {
		auto [m,refreshed] = _manual_refresh(*this,d) ;
		if (refreshed) for( Req r : j.reqs() ) r->audit_node(Color::Note,"manual_steady",idx()) ;
		return m ;
	}

	//
	// Target
	//

	::ostream& operator<<( ::ostream& os , Target const t ) {
		/**/                   os << "T("          ;
		if (+t               ) os << +t            ;
		if (t.is_unexpected()) os << ",unexpected" ;
		return                 os << ')'           ;
	}


	//
	// Deps
	//

	::ostream& operator<<( ::ostream& os , Deps const& ds ) {
		return os << vector_view_c<Dep>(ds) ;
	}

	//
	// Dep
	//

	::ostream& operator<<( ::ostream& os , Dep const& d ) {
		return os << static_cast<DepDigestBase<Node> const&>(d) ;
	}

	::string Dep::accesses_str() const {
		::string res ; res.reserve(+Access::N) ;
		for( Access a : Access::N ) res.push_back( accesses[a] ? AccessChars[+a] : '-' ) ;
		return res ;
	}

	::string Dep::dflags_str() const {
		::string res ; res.reserve(+Dflag::N) ;
		for( Dflag df : Dflag::N ) res.push_back( dflags[df] ? DflagChars[+df] : '-' ) ;
		return res ;
	}

}
