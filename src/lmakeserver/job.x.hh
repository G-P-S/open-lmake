// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

// included 3 times : with DEF_STRUCT defined, then with DATA_DEF defined, then with IMPL defined

#ifdef STRUCT_DECL
namespace Engine {

	struct Job        ;
	struct JobExec    ;
	struct JobTgt     ;
	struct JobTgts    ;
	struct JobData    ;
	struct JobReqInfo ;

	static constexpr uint8_t JobNGuardBits = 2 ;           // one to define JobTgt, the other to put it in a CrunchVector

}
#endif
#ifdef STRUCT_DEF
namespace Engine {

	ENUM( AncillaryTag
	,	Backend
	,	Data
	,	Dbg
	,	KeepTmp
	)

	ENUM_1( JobMakeAction
	,	Dec = Wakeup                   // if >=Dec => n_wait must be decremented
	,	None                           //                                         trigger analysis from dependent
	,	Wakeup                         //                                         a watched dep is available
	,	End                            // if >=End => job is ended,               job has completed
	,	GiveUp                         //                                         job has completed and no further analysis
	)

	ENUM( SpecialStep                  // ordered by increasing importance
	,	Idle
	,	NoFile
	,	Ok
	,	Err
	)

	struct ChronoDate {
		friend ::ostream& operator<<( ::ostream& , ChronoDate const& ) ;
		// cxtors & casts
		ChronoDate(           ) = default ;
		ChronoDate(bool is_end) ;
		// accesses
		bool operator==(ChronoDate const&) const = default ;
		bool operator+ (                 ) const { return chrono  ; }
		bool operator! (                 ) const { return !+*this ; }
		// data
		JobChrono chrono = 0 ;
		Pdate     date   ;
	} ;

	struct Job : JobBase {
		friend ::ostream& operator<<( ::ostream& , Job const ) ;
		using JobBase::side ;
		//
		using ReqInfo    = JobReqInfo    ;
		using MakeAction = JobMakeAction ;
		using Chrono     = JobChrono     ;
		// statics
		static bool   s_start_before_end( Chrono start , Chrono end ) {
			static_assert(::is_unsigned_v<Chrono>) ;                           // unsigned arithmetic are guaranteed to work modulo 2^n
			SWEAR( start && end , start , end ) ;                              // 0 is reserved to mean no info
			return Chrono(start-s_chrono) < Chrono(end-s_chrono) ;
		}
		static Chrono s_now(bool is_end) { s_tick(is_end) ; SWEAR(s_chrono) ; return s_chrono ; } // 0 is reserved to mean no info
		static Chrono s_now(           ) {                  SWEAR(s_chrono) ; return s_chrono ; } // .
		static void s_tick(bool is_end) {
			SWEAR(s_chrono) ;                                                  // 0 is reserved to mean no info
			if ( !s_chrono_is_end && is_end ) {
				/**/           s_chrono++ ;                                    // only incremented when seeing an start->end transition to save ticks as much as possible as chrono is only 32 bits
				if (!s_chrono) s_chrono++ ;                                    // 0 is reserved to mean no info
			}
			s_chrono_is_end = is_end ;
		}
		// static data
		static Chrono s_chrono        ;                                        // in case of equality start==end, start is posterior
		static bool   s_chrono_is_end ;
		// cxtors & casts
		using JobBase::JobBase ;
	private :
		Job( Rule::FullMatch&& , Req={} , DepDepth lvl=0 ) ;                   // plain Job, req is only for error reporting
	public :
		Job( RuleTgt , ::string const& t  , Req={} , DepDepth lvl=0 ) ;        // plain Job, match on target
		Job( Rule    , ::string const& jn , Req={} , DepDepth lvl=0 ) ;        // plain Job, match on name, for use when required from command line
		//
		Job( Special ,               Deps deps               ) ;               // Job used to represent a Req
		Job( Special , Node target , Deps deps               ) ;               // special job
		Job( Special , Node target , ::vector<JobTgt> const& ) ;               // multi
		// accesses
		bool active() const ;
	} ;

	struct JobTgt : Job {
		static_assert(Job::NGuardBits>=1) ;
		static constexpr uint8_t NGuardBits = Job::NGuardBits-1       ;
		static constexpr uint8_t NValBits   = NBits<Idx> - NGuardBits ;
		friend ::ostream& operator<<( ::ostream& , JobTgt ) ;
		// cxtors & casts
		JobTgt(                                                              ) = default ;
		JobTgt( Job j , bool is=false                                        ) : Job(j ) { is_sure( +j && is )   ; } // if no job, ensure JobTgt appears as false
		JobTgt( RuleTgt rt , ::string const& t , Req req={} , DepDepth lvl=0 ) ;
		JobTgt( JobTgt const& jt                                             ) : Job(jt) { is_sure(jt.is_sure()) ; }
		//
		JobTgt& operator=(JobTgt const& jt) { Job::operator=(jt) ; is_sure(jt.is_sure()) ; return *this ; }
		// accesses
		Idx operator+() const { return Job::operator+() | is_sure()<<(NValBits-1) ; }
		//
		bool is_sure(        ) const { return Job::side<1>(   ) ; }
		void is_sure(bool val)       {        Job::side<1>(val) ; }
		bool sure   (        ) const ;
		//
		template<uint8_t W,uint8_t LSB=0> requires( W>0 && W+LSB<=NGuardBits ) Idx  side(       ) const = delete ; // { return Job::side<W,LSB+1>(   ) ; }
		template<uint8_t W,uint8_t LSB=0> requires( W>0 && W+LSB<=NGuardBits ) void side(Idx val)       = delete ; // {        Job::side<W,LSB+1>(val) ; }
		// services
		bool produces( Node , bool sure=false ) const ;                        // if sure, reply true only if it is certain than node is produced
	} ;

	struct JobTgts : JobTgtsBase {
		friend ::ostream& operator<<( ::ostream& , JobTgts ) ;
		// cxtors & casts
		using JobTgtsBase::JobTgtsBase ;
	} ;

	struct JobExec : Job {
		friend ::ostream& operator<<( ::ostream& , JobExec const& ) ;
		// cxtors & casts
		JobExec() = default ;
		//
		JobExec( Job j , in_addr_t h , ChronoDate s={} , ChronoDate e={} ) : Job{j} , host{h} , start_{s} , end_{e} {}
		JobExec( Job j ,               ChronoDate s={} , ChronoDate e={} ) : Job{j} ,           start_{s} , end_{e} {}
		//
		JobExec( Job j , in_addr_t h , NewType           ) : Job{j} , host{h} , start_{false/*is_end*/}                        {} // starting job
		JobExec( Job j ,               NewType           ) : Job{j} ,           start_{false/*is_end*/}                        {} // .
		JobExec( Job j ,               NewType , NewType ) : Job{j} ,           start_{true /*is_end*/} , end_{true/*is_end*/} {} // instantaneous job, no need to distinguish start, cannot have host
		// services
		// called in main thread after start
		void report_start( ReqInfo&    , ::vmap<Node,bool/*uniquify*/> const& report_unlink={} , ::string const& stderr={} , ::string const& backend_msg={} ) const ;
		void report_start(                                                                                                                                  ) const ; // if started did not report
		void started     ( bool report , ::vmap<Node,bool/*uniquify*/> const& report_unlink={} , ::string const& stderr={} , ::string const& backecn_msg={} ) ;
		//
		void             live_out   ( ::string const&                                                         ) const ;
		JobRpcReply      job_info   ( JobProc , ::vector<Node> const& deps                                    ) const ; // answer to requests from job execution
		bool/*modified*/ end        ( ::vmap_ss const& rsrcs , JobDigest const& , ::string const& backend_msg ) ;       // hit indicates that result is from a cache hit
		void             continue_  ( Req , bool report=true                                                  ) ;       // Req is killed but job has some other req
		void             not_started(                                                                         ) ;       // Req was killed before it started
		//
		//
		void audit_end(
			::string    const& pfx
		,	ReqInfo     const&
		,	::string    const& backend_msg
		,	AnalysisErr const&
		,	::string    const& stderr
		,	size_t             max_stderr_len
		,	bool               modified
		,	Delay              exec_time   = {}
		) const ;
		void audit_end(
			::string    const& pfx
		,	ReqInfo     const& cri
		,	AnalysisErr const& ae
		,	::string    const& stderr
		,	size_t             max_stderr_len
		,	bool               modified
		,	Delay              exec_time   = {}
		) const {
			audit_end(pfx,cri,{}/*backend_msg*/,ae,stderr,max_stderr_len,modified,exec_time) ;
		}
		// data
		in_addr_t  host   = NoSockAddr ;
		ChronoDate start_ ;
		ChronoDate end_   ;
	} ;

}
#endif
#ifdef INFO_DEF
namespace Engine {

	struct JobReqInfo : ReqInfo {                                              // watchers of Job's are Node's
		friend ::ostream& operator<<( ::ostream& , JobReqInfo const& ) ;
		using Lvl        = JobLvl        ;
		using MakeAction = JobMakeAction ;
		// cxtors & casts
		using ReqInfo::ReqInfo ;
		// accesses
		bool running() const {
			switch (lvl) {
				case Lvl::Queued :
				case Lvl::Exec   : return true  ;
				default          : return false ;
			}
		}
		bool done(RunAction ra=RunAction::Status) const { return done_>=ra ; }
		// services
		void update( RunAction , MakeAction , JobData const& ) ;
		void add_watcher( Node watcher , NodeReqInfo& watcher_req_info ) { ReqInfo::add_watcher(watcher,watcher_req_info) ; }
		void chk() const {
			SWEAR(done_<=RunAction::Dsk) ;
			switch (lvl) {
				case Lvl::None   : SWEAR(n_wait==0) ; break ;                  // not started yet, cannot wait anything
				case Lvl::Done   : SWEAR(n_wait==0) ; break ;                  // done, cannot wait anything anymore
				case Lvl::Queued :
				case Lvl::Exec   : SWEAR(n_wait==1) ; break ;                  // if running, we are waiting for job execution
				default          : SWEAR(n_wait> 0) ; break ;                  // we must be waiting something if not Done nor None
			}
		}
		// data
		// req independent (identical for all Req's) : these fields are there as there is no Req-independent non-persistent table
		NodeIdx      dep_lvl          = 0                     ;                                      // 31<=32 bits
		JobChrono    end_chrono       = 0                     ;                                      //     32 bits, req independent
		ReqChrono    db_chrono        = 0                     ;                                      //     16 bits, req independent, oldest Req at which job is coherent (w.r.t. its state)
		RunAction    done_         :3 = RunAction   ::None    ; static_assert(+RunAction   ::N< 8) ; //      3 bits, action for which we are done
		Lvl          lvl           :3 = Lvl         ::None    ; static_assert(+Lvl         ::N< 8) ; //      3 bits
		BackendTag   backend       :2 = BackendTag  ::Unknown ; static_assert(+BackendTag  ::N< 4) ; //      2 bits
		JobReasonTag force         :5 = JobReasonTag::None    ; static_assert(+JobReasonTag::N<32) ; //      5 bits
		bool         start_reported:1 = false                 ;                                      //      1 bit , if true <=> start message has been reported to user
		bool         speculative   :1 = false                 ;                                      //      1 bit , if true <=> job is waiting for speculative deps only
	} ;
	static_assert(sizeof(JobReqInfo)==32) ;                                    // check expected size

}
#endif
#ifdef DATA_DEF
namespace Engine {

	ENUM_1( RunStatus
	,	Err = TargetErr                // >=Err means job is in error before even starting
	,	Complete                       // job was run
	,	NoDep                          // job was not run because of missing static dep
	,	NoFile                         // job was not run because it is a missing file in a source dir
	,	TargetErr                      // job was not run because of a manual static target
	,	DepErr                         // job was not run because of dep error
	,	RsrcsErr                       // job was not run because of resources could not be computed
	)

	struct JobData : DataBase {
		using Idx        = JobIdx        ;
		using ReqInfo    = JobReqInfo    ;
		using MakeAction = JobMakeAction ;
		using Chrono     = JobChrono     ;
		// static data
	private :
		static ::shared_mutex    _s_target_dirs_mutex ;
		static ::umap_s<NodeIdx> _s_target_dirs       ;    // dirs created for job execution that must not be deleted // XXX : use Node rather than string
		// cxtors & casts
	public :
		JobData() = default ;
		JobData( Special sp , Deps ds={} ) : deps{ds} , rule{sp} {             // special Job, all deps
			SWEAR(sp!=Special::Unknown) ;
			exec_gen = NExecGen ;                                              // special jobs are always exec_ok
		}
		JobData( Rule r , Deps sds ) : deps{sds} , rule{r} {                   // plain Job, static deps
			SWEAR(!rule.is_shared()) ;
		}
		//
		JobData           (JobData&& jd) : JobData(jd) {                                              jd.star_targets.forget() ; jd.deps.forget() ;                }
		~JobData          (            ) {                                                               star_targets.pop   () ;    deps.pop   () ;                }
		JobData& operator=(JobData&& jd) { SWEAR(rule==jd.rule,rule,jd.rule) ; *this = mk_const(jd) ; jd.star_targets.forget() ; jd.deps.forget() ; return *this ; }
	private :
		JobData           (JobData const&) = default ;
		JobData& operator=(JobData const&) = default ;
		// accesses
	public :
		Job      idx () const { return Job::s_idx(*this)              ; }
		::string name() const { return full_name(rule->job_sfx_len()) ; }
		//
		bool active() const { return !rule.old()                                                                   ; }
		bool is_src() const { return active() && (rule->special==Special::Src||rule->special==Special::GenericSrc) ; }
		//
		ReqInfo const& c_req_info   (Req           ) const ;
		ReqInfo      & req_info     (Req           ) const ;
		ReqInfo      & req_info     (ReqInfo const&) const ;                   // make R/W while avoiding look up (unless allocation)
		::vector<Req>  reqs         (              ) const ;
		::vector<Req>  running_reqs (              ) const ;
		::vector<Req>  old_done_reqs(              ) const ;
		//
		bool cmd_ok    (   ) const { return                      exec_gen >= rule->cmd_gen   ; }
		bool rsrcs_ok  (   ) const { return is_ok(status)!=No || exec_gen >= rule->rsrcs_gen ; } // dont care about rsrcs if job went ok
		bool frozen    (   ) const { return idx().frozen()                                   ; }
		bool is_special(   ) const { return rule->is_special() || frozen()                   ; }
		bool has_req   (Req) const ;
		//
		void exec_ok(bool ok) { SWEAR(!rule->is_special(),rule->special) ; exec_gen = ok ? rule->rsrcs_gen : 0 ; }
		//
		::pair<Delay,bool/*from_rule*/> best_exec_time() const {
			if (rule->is_special()) return { {}              , false } ;
			if (+exec_time        ) return {       exec_time , false } ;
			else                    return { rule->exec_time , true  } ;
		}
		//
		bool sure   () const ;
		void mk_sure()       { match_gen = Rule::s_match_gen ; _sure = true ; }
		bool err() const {
				if (run_status>=RunStatus::Err     ) return true               ;
				if (run_status!=RunStatus::Complete) return false              ;
				else                                 return is_ok(status)!=Yes ;
		}
		bool missing() const { return run_status<RunStatus::Err && run_status!=RunStatus::Complete ; }
		//
		ReqChrono db_chrono (           ) const {            for( Req r : reqs() ) if (ReqChrono c=c_req_info(r).db_chrono ) return c ; return 0 ; } // Req independent but is stored in req_info
		Chrono    end_chrono(           ) const {            for( Req r : reqs() ) if (Chrono    c=c_req_info(r).end_chrono) return c ; return 0 ; } // .
		void      db_chrono (ReqChrono c)       {            for( Req r : reqs() ) req_info(r).db_chrono  = c ;                                    } // .
		void      end_chrono(Chrono    c)       { SWEAR(c) ; for( Req r : reqs() ) req_info(r).end_chrono = c ;                                    } // .
		// services
		::pair<vmap_s<bool/*uniquify*/>,vmap<Node,bool/*uniquify*/>/*report*/> targets_to_wash(Rule::SimpleMatch const&) const ; // thread-safe
		::vmap<Node,bool/*uniquify*/>/*report*/                                wash           (Rule::SimpleMatch const&) const ; // thread-safe
		//
		void     end_exec      (                               ) const ;       // thread-safe
		::string ancillary_file(AncillaryTag=AncillaryTag::Data) const ;
		::string special_stderr(Node                           ) const ;
		::string special_stderr(                               ) const ;       // cannot declare a default value for incomplete type Node
		//
		void              invalidate_old() ;
		Rule::SimpleMatch simple_match  () const ;                             // thread-safe
		Rule::FullMatch   full_match    () const ;
		::vector<Node>    targets       () const ;
		//
		void set_pressure( ReqInfo& , CoarseDelay ) const ;
		//
		JobReason make( ReqInfo& , RunAction , JobReason={} , MakeAction=MakeAction::None , CoarseDelay const* old_exec_time=nullptr , bool wakeup_watchers=true ) ;
		//
		JobReason make( ReqInfo& ri , MakeAction ma ) { return make(ri,RunAction::None,{},ma) ; } // need same signature as for Node::make to use in templated watcher wake up
		//
		bool/*maybe_new_deps*/ submit( ReqInfo& , JobReason , CoarseDelay pressure ) ;
		//
		bool/*ok*/ forget( bool targets , bool deps ) ;
		//
		void add_watcher( ReqInfo& ri , Node watcher , NodeReqInfo& wri , CoarseDelay pressure ) ;
		//
		void audit_end_special( Req , SpecialStep , Bool3 modified , Node ) const ; // modified=Maybe means file is new
		void audit_end_special( Req , SpecialStep , Bool3 modified        ) const ; // cannot use default Node={} as Node is incomplete
		//
		template<class... A> void audit_end(A&&... args) const ;
	private :
		::pair<SpecialStep,Bool3/*modified*/> _update_target   (              Node target , ::string const& target_name , VarIdx target_idx=-1/*star*/ ) ;
		bool/*maybe_new_deps*/                _submit_special  ( ReqInfo&                                                                              ) ;
		bool                                  _targets_ok      ( Req        , Rule::SimpleMatch const&                                                 ) ;
		bool/*maybe_new_deps*/                _submit_plain    ( ReqInfo&   ,             JobReason ,              CoarseDelay pressure                ) ;
		void                                  _set_pressure_raw( ReqInfo&   , CoarseDelay                                                              ) const ;
		// data
	public :
		Targets          star_targets             ;                                                         //     32 bits, owned, for plain jobs
		Deps             deps                     ;                                                         // 31<=32 bits, owned
		Rule             rule                     ;                                                         //     16 bits,        can be retrieved from full_name, but would be slower
		CoarseDelay      exec_time                ;                                                         //     16 bits,        for plain jobs
		ExecGen          exec_gen  :NExecGenBits  = 0                   ;                                   //   <= 8 bits,        for plain jobs, cmd generation of rule
		mutable MatchGen match_gen :NMatchGenBits = 0                   ;                                   //   <= 8 bits,        if <Rule::s_match_gen => deemed !sure
		Tokens1          tokens1                  = 0                   ;                                   //   <= 8 bits,        for plain jobs, number of tokens - 1 for eta computation
		RunStatus        run_status:3             = RunStatus::Complete ; static_assert(+RunStatus::N< 8) ; //      3 bits
		Status           status    :4             = Status   ::New      ; static_assert(+Status   ::N<16) ; //      4 bits
	private :
		mutable bool     _sure     :1             = false               ;                                   //      1 bit
	} ;
	static_assert(sizeof(JobData)==20) ;                                       // check expected size

	ENUM( MissingAudit
	,	No
	,	Steady
	,	Modified
	)

}
#endif
#ifdef IMPL
namespace Engine {

	//
	// ChronoDate
	//

	inline ChronoDate::ChronoDate(bool is_end) : chrono{Job::s_now(is_end)} , date{Pdate::s_now()} {}

	//
	// Job
	//

	inline Job::Job( RuleTgt rt , ::string const& t  , Req req , DepDepth lvl ) : Job{Rule::FullMatch(rt,t ),req,lvl} {}
	inline Job::Job( Rule    r  , ::string const& jn , Req req , DepDepth lvl ) : Job{Rule::FullMatch(r ,jn),req,lvl} {}
	//
	inline Job::Job( Special sp ,          Deps deps ) : Job{                                New , sp,deps } { SWEAR(sp==Special::Req  ) ; }
	inline Job::Job( Special sp , Node t , Deps deps ) : Job{ {t->name(),Rule(sp).job_sfx()},New , sp,deps } { SWEAR(sp!=Special::Plain) ; }

	inline bool Job::active() const { return +*this && (*this)->active() ; }

	//
	// JobTgt
	//

	inline JobTgt::JobTgt( RuleTgt rt , ::string const& t , Req r , DepDepth lvl ) : JobTgt{ Job(rt,t,r,lvl) , rt.sure() } {}

	inline bool JobTgt::sure() const { return is_sure() && (*this)->sure() ; }

	inline bool JobTgt::produces( Node t , bool sure ) const {
		if ((*this)->missing()          ) return false ;                       // missing jobs produce nothing
		if (is_sure()                   ) return true  ;
		if ((*this)->err()              ) return !sure ;                       // jobs in error are deemed to produce all their potential targets
		if (t->has_actual_job_tgt(*this)) return true  ;                       // fast path
		//
		return ::binary_search( (*this)->star_targets , t ) ;
	}

	//
	// JobData
	//

	inline ::string JobData::special_stderr   (                                 ) const { return special_stderr   (      {}) ; }
	inline void     JobData::audit_end_special( Req r , SpecialStep s , Bool3 m ) const { return audit_end_special(r,s,m,{}) ; }

	inline Rule::SimpleMatch JobData::simple_match() const { return Rule::SimpleMatch(idx()) ; }
	inline Rule::FullMatch   JobData::full_match  () const { return Rule::FullMatch  (idx()) ; }

	inline void JobData::invalidate_old() {
		if ( +rule && rule.old() ) idx().pop() ;
	}

	inline void JobData::add_watcher( ReqInfo& ri , Node watcher , Node::ReqInfo& wri , CoarseDelay pressure ) {
		ri.add_watcher(watcher,wri) ;
		set_pressure(ri,pressure) ;
	}

	inline void JobData::set_pressure(ReqInfo& ri , CoarseDelay pressure ) const {
		if (!ri.set_pressure(pressure)) return ;                                   // if pressure is not significantly higher than already existing, nothing to propagate
		if (!ri.waiting()             ) return ;
		_set_pressure_raw(ri,pressure) ;
	}

	inline bool/*maybe_new_deps*/ JobData::submit( ReqInfo& ri , JobReason reason , CoarseDelay pressure ) {
		ri.force = JobReasonTag::None ;                                                                      // job is submitted, that was the goal, now void looping
		if (is_special()) return _submit_special(ri                ) ;
		else              return _submit_plain  (ri,reason,pressure) ;
	}

	template<class... A> inline void JobData::audit_end(A&&... args) const {
		JobExec(idx(),New,New).audit_end(::forward<A>(args)...) ;
	}

	inline bool JobData::sure() const {
		if (match_gen<Rule::s_match_gen) {
			_sure     = false             ;
			match_gen = Rule::s_match_gen ;
			if (!rule->is_sure()) goto Return ;
			for( Dep const& d : deps ) {
				if (!d.dflags[Dflag::Static]) continue    ;                    // we are only interested in static targets, other ones may not exist and do not prevent job from being built
				if (d->buildable!=Yes       ) goto Return ;
			}
			_sure = true ;
		}
	Return :
		return _sure ;
	}

	inline JobData::ReqInfo const& JobData::c_req_info(Req r) const {
		::umap<Job,ReqInfo> const& req_infos = Req::s_store[+r].jobs ;
		auto                       it        = req_infos.find(idx()) ;         // avoid double look up
		if (it==req_infos.end()) return Req::s_store[+r].jobs.dflt ;
		else                     return it->second                 ;
	}
	inline JobData::ReqInfo& JobData::req_info(Req req) const {
		ReqInfo& ri = Req::s_store[+req].jobs.try_emplace(idx(),ReqInfo(req)).first->second ;
		for( Req r : reqs() ) {
			if (r==req) continue ;
			ri.db_chrono  = c_req_info(r).db_chrono  ;                         // copy Req independent fields from any other ReqInfo (they are all identical)
			ri.end_chrono = c_req_info(r).end_chrono ;                         // .
			break ;
		}
		return ri ;
	}
	inline JobData::ReqInfo& JobData::req_info(ReqInfo const& cri) const {
		if (&cri==&Req::s_store[+cri.req].jobs.dflt) return req_info(cri.req)         ; // allocate
		else                                         return const_cast<ReqInfo&>(cri) ; // already allocated, no look up
	}
	inline ::vector<Req> JobData::reqs() const { return Req::reqs(*this) ; }

	inline bool JobData::has_req(Req r) const {
		return Req::s_store[+r].jobs.contains(idx()) ;
	}

	//
	// JobReqInfo
	//

	inline void JobReqInfo::update( RunAction run_action , MakeAction make_action , JobData const& job ) {
		Bool3 ok = is_ok(job.status) ;
		if ( ok==Maybe && action>=RunAction::Status ) run_action = RunAction::Run ;
		if (make_action>=MakeAction::Dec) {
			SWEAR(n_wait) ;
			n_wait-- ;
		}
		if (run_action>action) {                                               // increasing action requires to reset checks
			lvl     = lvl & Lvl::Dep ;
			dep_lvl = 0              ;
			action  = run_action     ;
		}
		if (n_wait) {
			SWEAR( make_action<MakeAction::End , make_action ) ;
		} else if (
			( req->zombie                              )                       // zombie's need not check anything
		||	( make_action==MakeAction::GiveUp          )                       // if not started, no further analysis
		||	( action==RunAction::Makable && job.sure() )                       // no need to check deps, they are guaranteed ok if sure
		) {
			done_ = done_ | action ;
		} else if (make_action==MakeAction::End) {
			lvl     = lvl & Lvl::Dep ;                                         // we just ran, reset analysis
			dep_lvl = 0              ;
			action  = run_action     ;                                         // we just ran, we are allowed to decrease action
		}
		if (done_>=action) lvl = Lvl::Done ;
		SWEAR(lvl!=Lvl::End) ;
	}

}
#endif
