// This code supplies the ability to create callbacks for Scheduler.Timer.
// Beside being able to call back into a member function, it also binds the
// argument to the callback function.
//
// No doubt this could be done more tersely with various std::tr1 && boost 
// bind and functor capabilities, but I didn't want to depend on those libs.
//
// Adapted for the wang 2200 emulator, 2008
// Author: Jim Battle, 2007

#ifndef _INCLUDE_CALLBACK_H_
#define _INCLUDE_CALLBACK_H_

// define a function signature for the timer callback.
// this code modified from Herb Sutter's:
//     http://www.gotw.ca/gotw/083.htm

class CallbackBase
{
public:
    virtual void operator()() const { };
    virtual ~CallbackBase() = 0;
};

// Create a callback that stores the parameter to be sent to the callback
// function.  For instance,
//
//    void TimerTestFoo::report(int i)
//    { printf("got callback for timer %d after %d clocks\n", i, g_testtime); }
//
//    Callback<TimerTestFoo, int> cb1 = make_callback(foo, &TimerTestFoo::report, 33);
//    when the cb1 callback is triggered, 33 is sent to report()

template<class T, typename P>
class Callback : public CallbackBase
{
public:
    typedef void (T::*F)(P);  // class T, member func F, param P

    Callback( T& t, F f, P p ) : t_(&t), f_(f), p_(p) { }
    void operator()() const { (t_->*f_)(p_); }

private:
    T* t_;
    F  f_;
    P  p_;

    // this would actually work, but we don't need it
    CANT_ASSIGN_OR_COPY_CLASS(Callback);
};

// return a statically allocated callback object.
// it must be copied to the caller's local storage at the point of call;
// there are no allocations going on here.
template<class T, typename P>
Callback<T,P> make_callback( T& t, void (T::*f)(P), P p )
{
    return Callback<T,P>( t, f, p );
}

#endif // _INCLUDE_CALLBACK_H_
