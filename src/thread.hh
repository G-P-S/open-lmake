// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#pragma once

#include <thread>

#include "time.hh"

template<class T> struct ThreadQueue : private ::deque<T> {
private :
	using Base = ::deque<T> ;
public :
	using Base::empty ;
	using Base::size  ;
	// cxtors & casts
	ThreadQueue() = default ;
	bool operator+() const {
		::unique_lock lock{_mutex} ;
		return !Base::empty() ;
	}
	bool operator!() const { return !+*this ; }
	// services
	/**/                 void push   (T const& x) { ::unique_lock lock{_mutex} ; Base::push_back   (x                 ) ; _cond.notify_one() ; }
	template<class... A> void emplace(A&&...   a) { ::unique_lock lock{_mutex} ; Base::emplace_back(::forward<A>(a)...) ; _cond.notify_one() ; }
	T pop() {
		::unique_lock lock{_mutex} ;
		_cond.wait( lock , [&](){ return !Base::empty() ; } ) ;
		Time::Ddate::s_refresh_now() ;                                         // we have waited, refresh Ddate::s_now()
		return _pop() ;
	}
	::pair<bool/*popped*/,T> try_pop() {
		::unique_lock lock{_mutex} ;
		if (empty()) return {false/*popped*/,T()} ;
		return {true/*popped*/,_pop()} ;
	}
	::pair<bool/*popped*/,T> pop(::stop_token tkn) {
		::unique_lock lock{_mutex} ;
		if (!_cond.wait( lock , tkn , [&](){ return !Base::empty() ; } )) return {false/*popped*/,T()} ;
		return {true/*popped*/,_pop()} ;
	}
private :
	T _pop() {
		T res = ::move(Base::front()) ;
		Base::pop_front() ;
		return res ;
	}
	// data
	::mutex mutable          _mutex ;
	::condition_variable_any _cond  ;
} ;

template<class Item> struct QueueThread {
	using Ddate = Time::Ddate                     ;
	using Pdate = Time::Pdate                     ;
	using Queue = ThreadQueue<::pair<Pdate,Item>> ;
private :
	static void _s_thread_func( ::stop_token stop , char key , QueueThread* self , ::function<void(Item&&)> func ) {
		Trace::t_key = key ;
		Trace trace("_s_thread_func") ;
		for(;;) {
			auto [popped,info] = self->_queue.pop(stop) ;
			if ( !popped                       ) break ;
			if ( !info.first.sleep_until(stop) ) break ;
			Ddate::s_refresh_now() ;                                           // we have waited, refresh now
			func(::move(info.second)) ;
		}
		trace("done") ;
	}
	// cxtors & casts
public :
	QueueThread(        ) = default ;
	QueueThread( char key , ::function<void(Item&&)> func ) : _thread{_s_thread_func,key,this,func} {}
	// services
	/**/                 void push      (           Item const& x ) { _queue.push({Pdate(),x})                         ; }
	/**/                 void push_at   ( Pdate d , Item const& x ) { _queue.push({d      ,x})                         ; }
	template<class... A> void emplace   (           A&&...      a ) { _queue.emplace(Pdate(),Item(::forward<A>(a)...)) ; }
	template<class... A> void emplace_at( Pdate d , A&&...      a ) { _queue.emplace(d      ,Item(::forward<A>(a)...)) ; }
	// data
private :
	Queue     _queue  ;
	::jthread _thread ;                // ensure _thread is last so other fields are constructed when it starts
} ;

ENUM(EventKind
,	Std
,	Master
,	Slave
,	Int
,	Stop
)
template<class Req> struct ServerThread {
	using Ddate = Time::Ddate ;
private :
	static void _s_thread_func( ::stop_token stop , char key , ServerThread* self , ::function<bool/*keep_fd*/(Req&&,Fd)> func ) {
		static constexpr uint64_t One = 1 ;
		Trace::t_key = key ;
		AutoCloseFd        stop_fd = ::eventfd(0,O_CLOEXEC) ; stop_fd.no_std() ;
		Epoll              epoll   { New }                  ;
		::umap<Fd,IMsgBuf> slaves  ;
		::stop_callback    stop_cb {                                           // transform request_stop into an event Epoll can wait for
			stop
		,	[&](){
				ssize_t cnt = ::write(stop_fd,&One,sizeof(One)) ;
				SWEAR( cnt==sizeof(One) , cnt ) ;
			}
		} ;
		//
		Trace trace("_s_thread_func",self->fd.port()) ;
		self->_ready.count_down() ;
		//
		epoll.add_read(self->fd,EventKind::Master) ;
		epoll.add_read(stop_fd ,EventKind::Stop  ) ;
		for(;;) {
			trace("wait") ;
			::vector<Epoll::Event> events = epoll.wait() ;                     // wait for 1 event, no timeout
			for( Epoll::Event event : events ) {
				EventKind kind = event.data<EventKind>() ;
				Fd        efd  = event.fd()              ;
				trace("waited",efd,kind) ;
				switch (kind) {
					case EventKind::Master : {
						SWEAR(efd==self->fd) ;
						SlaveSockFd slave_fd{self->fd.accept()} ;
						trace("new_req",slave_fd) ;
						epoll.add_read(slave_fd,EventKind::Slave) ;
						slaves.try_emplace(::move(slave_fd)) ;
					} break ;
					case EventKind::Stop : {
						uint64_t _   ;
						ssize_t  cnt = ::read(efd,&_,sizeof(_)) ;
						SWEAR( cnt==sizeof(_) , cnt ) ;
						for( auto const& [sfd,_] : slaves ) epoll.close(sfd) ;
						trace("done") ;
						return ;
					} break ;
					case EventKind::Slave : {
						Req r ;
						try         { if (!slaves.at(efd).receive_step(efd,r)) { trace("partial") ; continue ; } }
						catch (...) {                                            trace("bad_msg") ; continue ;   } // ignore malformed messages
						//
						epoll.del(efd) ;                                       // Func may trigger efd being closed by another thread, hence epoll.del must be done before
						slaves.erase(efd) ;
						if (!func(::move(r),efd)) { efd.close() ; }            // close efd if not requested to keep it
					} break ;
					default : FAIL(kind) ;
				}
			}
		}
		trace("done") ;
	}
	// cxtors & casts
public :
	ServerThread(char key, ::function<bool/*keep_fd*/(Req&&,Fd)> func ,int backlog=0) : fd{New,backlog} , _thread{_s_thread_func,key,this,func} {}
	// services
	void wait_started() {
		_ready.wait() ;
	}
	// data
	ServerSockFd fd ;
private :
	::latch   _ready  {1} ;
	::jthread _thread ;                // ensure _thread is last so other fields are constructed when it starts
} ;