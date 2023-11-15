// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#pragma once

#include <chrono>
#include <thread>

#include <ctime>

#include <iomanip>

#include "utils.hh"

namespace Time {

	struct Delay       ;
	struct CoarseDelay ;
	struct Date        ;
	struct Ddate       ;
	struct Pdate       ;
	template<class T> requires(IsOneOf<T,int64_t,uint64_t>) struct TimeBase ;

	template<class T> requires(IsOneOf<T,int64_t,uint64_t>) struct TimeBase {
		friend CoarseDelay ;
		static constexpr T    TicksPerSecond = 1'000'000'000l                 ; // if modified some methods have to be rewritten, as indicated by static asserts
		static constexpr bool IsUnsigned     = ::is_unsigned_v<T>             ;
		static constexpr bool IsNs           = TicksPerSecond==1'000'000'000l ;
		//
		using Tick     = T                                            ;
		using T32      = ::conditional_t<IsUnsigned,uint32_t,int32_t> ;
		using TimeSpec = struct ::timespec                            ;
		using TimeVal  = struct ::timeval                             ;
		// cxtors & casts
		constexpr          TimeBase(                  )                      = default ;
		constexpr explicit TimeBase(int             v )                      : _val( v*TicksPerSecond                           ) { if (IsUnsigned) SWEAR( v>=0 , v ) ; }
		constexpr explicit TimeBase(long            v )                      : _val( v*TicksPerSecond                           ) { if (IsUnsigned) SWEAR( v>=0 , v ) ; }
		constexpr explicit TimeBase(unsigned int    v ) requires(IsUnsigned) : _val( v*TicksPerSecond                           ) {                                     }
		constexpr explicit TimeBase(unsigned long   v ) requires(IsUnsigned) : _val( v*TicksPerSecond                           ) {                                     }
		constexpr explicit TimeBase(double          v )                      : _val( v*TicksPerSecond                           ) { if (IsUnsigned) SWEAR( v>=0 , v ) ; }
		constexpr explicit TimeBase(float           v )                      : _val( v*TicksPerSecond                           ) { if (IsUnsigned) SWEAR( v>=0 , v ) ; }
		constexpr explicit TimeBase(TimeSpec const& ts)                      : _val( ts.tv_sec*TicksPerSecond + ts.tv_nsec      ) { static_assert(IsNs) ; if (IsUnsigned) SWEAR(ts.tv_sec>=0) ; }
		constexpr explicit TimeBase(TimeVal  const& tv)                      : _val( tv.tv_sec*TicksPerSecond + tv.tv_usec*1000 ) { static_assert(IsNs) ; if (IsUnsigned) SWEAR(tv.tv_sec>=0) ; }
	protected :
		constexpr explicit TimeBase(NewType,T       v ) : _val( v                                          ) {}
		//
	public :
		constexpr explicit operator TimeSpec() const { TimeSpec ts{ .tv_sec=sec() , .tv_nsec=nsec_in_s() } ; return ts                          ; }
		constexpr explicit operator T       () const {                                                       return _val                        ; }
		constexpr explicit operator double  () const {                                                       return double(_val)/TicksPerSecond ; }
		constexpr explicit operator float   () const {                                                       return float (_val)/TicksPerSecond ; }
		// accesses
		constexpr T    operator+() const { return  _val ; }
		constexpr bool operator!() const { return !_val ; }
		//
		constexpr T   sec      () const {                       return _val/TicksPerSecond   ; }
		constexpr T   nsec     () const { static_assert(IsNs) ; return _val                  ; }
		constexpr T32 nsec_in_s() const { static_assert(IsNs) ; return _val%TicksPerSecond   ; }
		constexpr T   usec     () const {                       return nsec     ()/1000      ; }
		constexpr T32 usec_in_s() const {                       return nsec_in_s()/1000      ; }
		constexpr T   msec     () const {                       return nsec     ()/1000'000l ; }
		constexpr T32 msec_in_s() const {                       return nsec_in_s()/1000'000  ; }
		//
		void clear() { _val = 0 ; }
		// data
	protected :
		T _val = 0 ;
	} ;

	struct Delay : TimeBase<int64_t> {
		using Base = TimeBase<int64_t> ;
		friend ::ostream& operator<<( ::ostream& , Delay const ) ;
		friend Date  ;
		friend Ddate ;
		friend Pdate ;
		// statics
	private :
		static bool/*slept*/ _s_sleep( ::stop_token tkn , Delay sleep , Pdate until ) ;
		// cxtors & casts
	public :
		using Base::Base ;
		constexpr Delay(Base v) : Base(v) {}
		// services
		constexpr bool              operator== (Delay const& other) const { return _val== other._val  ; } // C++ requires a direct compare to support <=>
		constexpr ::strong_ordering operator<=>(Delay const& other) const { return _val<=>other._val  ; }
		//
		using Base::operator+ ;
		constexpr Delay  operator+ (Delay other) const {                         return Delay(New,_val+other._val) ; }
		constexpr Delay  operator- (Delay other) const {                         return Delay(New,_val-other._val) ; }
		constexpr Delay& operator+=(Delay other)       { *this = *this + other ; return *this                      ; }
		constexpr Delay& operator-=(Delay other)       { *this = *this - other ; return *this                      ; }
		constexpr Date   operator+ (Date       ) const ;
		//
		template<class T> requires(::is_arithmetic_v<T>) constexpr Delay  operator* (T f) const ;
		template<class T> requires(::is_signed_v    <T>) constexpr Delay  operator/ (T f) const ;
		template<class T> requires(::is_unsigned_v  <T>) constexpr Delay  operator/ (T f) const ;
		template<class T> requires(::is_arithmetic_v<T>) constexpr Delay& operator*=(T f)       { *this = *this*f ; return *this ; }
		template<class T> requires(::is_arithmetic_v<T>) constexpr Delay& operator/=(T f)       { *this = *this/f ; return *this ; }
		//
		bool/*slept*/ sleep_for(::stop_token) const ;
		void          sleep_for(            ) const ;
		//
		::string short_str() const ;
	} ;

	// short float representation of time (positive)
	// when exp<=0, representation is linear after TicksPerSecond
	// else       , it is floating point
	struct CoarseDelay {
		friend ::ostream& operator<<( ::ostream& , CoarseDelay const ) ;
		using Val = uint16_t ;
		static constexpr int64_t  TicksPerSecond = 1000                 ;      // this may be freely modified
		static constexpr uint8_t  Mantissa       = 11                   ;      // .
		static constexpr uint32_t Scale          = 28294                ;      // (::logf(Delay::TicksPerSecond)-::logf(TicksPerSecond))*(1<<Mantissa) ;
		//
		static const CoarseDelay MinPositive ;
		// statics
	private :
		static constexpr Val _Factor(uint32_t percent) { return (1<<Mantissa)*percent/100 ; }
		// cxtors & casts
	public :
		constexpr CoarseDelay() = default ;
		constexpr CoarseDelay(Delay d) { *this = d ; }
		constexpr CoarseDelay& operator=(Delay d) {
			uint32_t t = ::logf(+d)*(1<<Mantissa)+0.5 ;
			if      ( t >= (1<<NBits<Val>)+Scale ) _val = -1      ;
			else if ( t <                  Scale ) _val =  0      ;
			else                                   _val = t-Scale ;
			return *this ;
		}
		explicit constexpr CoarseDelay(Val v) : _val(v) {}
		constexpr operator Delay() const {
			if (!_val) return Delay() ;
			else       return Delay(New,int64_t(::expf(float(_val+Scale)/(1<<Mantissa)))) ;
		}
		constexpr explicit operator double() const { return double(Delay(*this)) ; }
		constexpr explicit operator float () const { return float (Delay(*this)) ; }
		// services
		constexpr uint16_t          operator+  (                    ) const { return _val ;                      }
		constexpr bool              operator!  (                    ) const { return !+*this ;                   }
		constexpr CoarseDelay       operator+  (Delay              d) const { return Delay(*this) + d ;          }
		constexpr CoarseDelay&      operator+= (Delay              d)       { *this = *this + d ; return *this ; }
		constexpr bool              operator== (CoarseDelay const& d) const { return _val== d._val ;             }
		constexpr ::strong_ordering operator<=>(CoarseDelay const& d) const { return _val<=>d._val ;             }
		//
		CoarseDelay scale_up  (uint32_t percent) const { return CoarseDelay( _val>=Val(-1)-_Factor(percent) ? Val(-1) : Val(_val+_Factor(percent)) ) ; }
		CoarseDelay scale_down(uint32_t percent) const { return CoarseDelay( _val<=        _Factor(percent) ? Val( 0) : Val(_val-_Factor(percent)) ) ; }
		//
		::string short_str() const { return Delay(*this).short_str() ; }
		// data
	private :
		Val _val = 0 ;
	} ;
	constexpr CoarseDelay CoarseDelay::MinPositive = CoarseDelay(CoarseDelay::Val(1)) ;

	struct Date : TimeBase<uint64_t> {
		using Base = TimeBase<uint64_t> ;
		friend ::ostream& operator<<( ::ostream& , Date const ) ;
		friend Delay ;
		static const Date None ;
		// cxtors & casts
		using Base::Base ;
		Date(::string_view const&) ;                                           // read a reasonable approximation of ISO8601
		// services
		using Base::operator+ ;
		constexpr Date  operator+ (Delay other) const {                         return Date(New,_val+other._val) ; }
		constexpr Date  operator- (Delay other) const {                         return Date(New,_val-other._val) ; }
		constexpr Date& operator+=(Delay other)       { *this = *this + other ; return *this                     ; }
		constexpr Date& operator-=(Delay other)       { *this = *this - other ; return *this                     ; }
		//
		::string str( uint8_t prec=0 , bool in_day=false ) const ;
	} ;

	//
	// We implement a complete separation between wall-clock time (Pdate) a short for process date) and time seen from the disk (Ddate) which may be on a server with its own view of time.
	// Care has been taken so that you cannot compare and more generally inter-operate between these 2 times.
	// Getting current Pdate-time is very cheap (few ns), so no particular effort is made to cache or otherwise optimize it.
	// But it is the contrary for Ddate current time : you must create or write to a file, very expensive (some fraction of ms).
	// So we keep a lazy evaluated cached value that is refreshed once per loop (after we have waited) in each thread :
	// - in terms of precision, this is enough, we just want correct relative order
	// - in terms of cost, needing current disk time is quite rare (actually, we just need it to put a date on when a file is known to not exist, else we have a file date)
	// - so in case of exceptional heavy use, cached value is used and in case of no use, we do not pay at all.
	//

	struct Pdate : Date {
		friend ::ostream& operator<<( ::ostream& , Pdate const ) ;
		friend Delay ;
		// statics
		static Pdate s_now() ;
		// cxtors & casts
		using Date::Date ;
		// services
		constexpr bool              operator== (Pdate const& other) const { return _val== other._val  ; } // C++ requires a direct compare to support <=>
		constexpr ::strong_ordering operator<=>(Pdate const& other) const { return _val<=>other._val  ; }
		//
		using Base::operator+ ;
		constexpr Pdate  operator+ (Delay other) const {                         return Pdate(New,_val+other._val) ; }
		constexpr Pdate  operator- (Delay other) const {                         return Pdate(New,_val-other._val) ; }
		constexpr Pdate& operator+=(Delay other)       { *this = *this + other ; return *this                       ; }
		constexpr Pdate& operator-=(Delay other)       { *this = *this - other ; return *this                       ; }
		constexpr Delay  operator- (Pdate      ) const ;
		//
		bool/*slept*/ sleep_until(::stop_token) const ;
		void          sleep_until(            ) const ;
	} ;

	struct Ddate : Date {
		friend ::ostream& operator<<( ::ostream& , Ddate const ) ;
		friend Delay ;
		// statics
		// s_now is rarely but unpredictably used
		// so the idea is that s_refresh_now is called after each wait (i.e. once per loop in each thread)
		// but this is cheap, you only pay if you actually call s_now, and this is the rare event
		static void  s_refresh_now() { _t_now = {} ;                        }  // refresh s_now (actual clear cached value)
		static Ddate s_now        () { return +_t_now ? _t_now : _s_now() ; }  // provide a disk view of now
	private :
		static Ddate _s_now() ;                                                // update cached value and return it
		// static data
		static thread_local Ddate _t_now ;                                     // per thread lazy evaluated cached value
		// cxtors & casts
	public :
		using Date::Date ;
		// services
		constexpr bool              operator== (Ddate const& other) const { return _val== other._val  ; } // C++ requires a direct compare to support <=>
		constexpr ::strong_ordering operator<=>(Ddate const& other) const { return _val<=>other._val  ; }
		//
		using Base::operator+ ;
		constexpr Ddate  operator+ (Delay other) const {                         return Ddate(New,_val+other._val) ; }
		constexpr Ddate  operator- (Delay other) const {                         return Ddate(New,_val-other._val) ; }
		constexpr Ddate& operator+=(Delay other)       { *this = *this + other ; return *this                      ; }
		constexpr Ddate& operator-=(Delay other)       { *this = *this - other ; return *this                      ; }
		constexpr Delay  operator- (Ddate      ) const ;
		//
		bool/*slept*/ sleep_until(::stop_token) const ;
		void          sleep_until(            ) const ;
	} ;

	//
	// implementation
	//

	//
	// Delay
	//
	inline constexpr Date Delay::operator+(Date d) const {
		return Date(New,_val+d._val) ;
	}
	inline bool/*slept*/ Delay::_s_sleep( ::stop_token tkn , Delay sleep , Pdate until ) {
		if (sleep<=Delay()) return !tkn.stop_requested() ;
		::mutex                  m   ;
		::unique_lock<mutex>     lck { m } ;
		::condition_variable_any cv  ;
		bool res = cv.wait_for( lck , tkn , ::chrono::nanoseconds(sleep.nsec()) , [until](){ return Pdate::s_now()>=until ; } ) ;
		return res ;
	}
	inline bool/*slept*/ Delay::sleep_for(::stop_token tkn) const {
		return _s_sleep( tkn , *this , Pdate::s_now()+*this ) ;
	}
	inline void Delay::sleep_for() const {
		if (_val<=0) return ;
		TimeSpec ts(*this) ;
		::nanosleep(&ts,nullptr) ;
	}
	template<class T> requires(::is_arithmetic_v<T>) inline constexpr Delay Delay::operator*(T f) const { return Delay(New,int64_t(_val*                   f )) ; }
	template<class T> requires(::is_signed_v    <T>) inline constexpr Delay Delay::operator/(T f) const { return Delay(New,int64_t(_val/                   f )) ; }
	template<class T> requires(::is_unsigned_v  <T>) inline constexpr Delay Delay::operator/(T f) const { return Delay(New,int64_t(_val/::make_signed_t<T>(f))) ; }

	//
	// Date
	//
	constexpr Date Date::None { New , uint64_t(0) } ;
	inline Pdate Pdate::s_now() {
		TimeSpec now ;
		::clock_gettime(CLOCK_REALTIME,&now) ;
		return Pdate(now) ;
	}
	inline constexpr Delay Pdate::operator-(Pdate other) const { return Delay(New,int64_t(_val-other._val)) ; }
	inline constexpr Delay Ddate::operator-(Ddate other) const { return Delay(New,int64_t(_val-other._val)) ; }
	//
	inline bool/*slept*/ Pdate::sleep_until(::stop_token tkn) const { return Delay::_s_sleep( tkn , *this-s_now() , *this ) ; }
	inline void          Pdate::sleep_until(                ) const { (*this-s_now()).sleep_for()                           ; }

}

// must be outside Engine namespace as it specializes ::hash
namespace std {
	template<> struct hash<Time::Date       > { size_t operator()(Time::Date        d) const { return +d ; } } ;
	template<> struct hash<Time::Delay      > { size_t operator()(Time::Delay       d) const { return +d ; } } ;
	template<> struct hash<Time::CoarseDelay> { size_t operator()(Time::CoarseDelay d) const { return +d ; } } ;
}
