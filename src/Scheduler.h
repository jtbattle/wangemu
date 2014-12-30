// This is a simple minded event scheduler.  A finite number of timers can be
// outstanding at any given time, which is not an issue for its intended use.
// See the corresponding .cpp for more details of its history and function.

#ifndef _INCLUDE_SCHEDULER_H_
#define _INCLUDE_SCHEDULER_H_

#include "w2200.h"  // pick up def of int32

// simple callback mechanism
#include "Callback.h"

// for now, statically define how many timers can be active.
// in the future we could use a vector container to grow automatically,
// which is cleaner but a bit slower.
#define NUM_TIMERS 10

// fwd reference
class Timer;

// this class manages event-driven behavior for the emulator.
// time advances every cpu tick, and callers can request to be called
// back with a specific parameter after some number of cycles.
// timers can also be killed before they have come due.
class Scheduler
{
public:
    Scheduler();
    ~Scheduler();

    // create a new timer; an int handle is returned.
    // this handle can be used to identify the timer later.
    // ticks is the number of clock ticks before the callback fires,
    // passing back the stored arg.
    template <class T, typename P>
    Timer* TimerCreate(int ticks, Callback<T,P> &fcn) {
        Callback<T,P> *fcncopy = new Callback<T,P>(fcn);
        return TimerCreateImpl(ticks, fcncopy);
    }

    // specify callback without and intermediate callback function, e.g.
    //   void TimerTestFoo::report(int i)
    //   { printf("got callback for timer %d after %d clocks\n", i, g_testtime); }
    //
    //   Timer* tmr = TimerCreate( 100, foo, &TimerTestFoo:report, 33 );
    //
    // After 100 clocks, TimerTestFoo::report(33) is called.
    template <class T, typename P>
    Timer* TimerCreate(int ticks, T& t, void (T::*f)(P), P p) {
        return TimerCreateImpl(ticks, new Callback<T,P>(t, f, p));
    }

    // remove a pending timer event by passing its index
    // (called only from Timer.Kill())
    void TimerKill(int n);

    // let 'n' cpu cycles of simulated time go past
    inline void TimerTick(int n)
    {
        m_countdown -= n;
        if (m_countdown <= 0)
            TimerCredit();
    }

    // when computing time deltas, we don't want to get confused
    // my negative numbers and wrapping
    const static int MAX_TICKS = (1 << 30)-1;

private:
    // actual implementation
    Timer* TimerCreateImpl(int ticks, CallbackBase *fcn);

    // transfer accumulated timer deficit to each active timer.
    // n is added to the already elapsed time.
    // this shouldn't need to be called very frequently.
    void TimerCredit();

    // semaphore that indicates TimerCredit() is running so that
    // we can double check that nobody calls TimerTick() until
    // from a callback function.
    bool m_updating;

    // rather than updating all N counters every instruction, we instead
    // find the one which will expire first, and transfer that into
    // countdown and startcnt.  as simulated time ticks by, we decrement
    // countdown.
    //
    // when countdown goes <=0, all timers have (startcnt-countdown)
    // ticks removed from their own counter.
    int32 m_countdown;
    int32 m_startcnt;

    // any timer that has its own counter go <=0 will then
    // cause a callback to the supplied function using the
    // supplied parameters.
    struct {
        int32         ctr;      // counter until expiration
        CallbackBase *callback; // registered callback function
        Timer        *ptmr;     // timer handle
    } m_timer[NUM_TIMERS];

    // list of free timers
    int m_numFree;
    int m_freeIdx[NUM_TIMERS];

    // list of active timers
    int m_numActive;
    int m_activeIdx[NUM_TIMERS];

    // list of expired timers
    int m_numRetired;
    int m_retiredIdx[NUM_TIMERS];
};

// scale a floating point time in microseconds to be an
// argument appropriate for the TimerCreate() function.
#define TIMER_US(f) (int(   10.0*(f)+0.5))
#define TIMER_MS(f) (int(10000.0*(f)+0.5))

// this is used to kill off a timer that may or may not be inactive
#define ENSURE_TIMER_DEAD(thnd) {       \
        if ((thnd) != 0) {              \
            (thnd)->Kill();             \
            (thnd) = 0;                 \
        }                               \
    } while (0)

// ======================================================================
// This is just a handle that Scheduler can pass back on timer creation,
// making it more natural for the recipient to manipulate the timer later.

class Timer
{
public:
    Timer(Scheduler *sched, int i) : s(sched), idx(i) { };
    ~Timer() { };

    // kill off this timer
    void Kill();

private:
    Scheduler * const s;  // pointer to owning scheduler
    int idx;              // index into the array that Scheduler keeps

    // actually I think it would be safe, but there is no need to do this
    CANT_ASSIGN_OR_COPY_CLASS(Timer);
};

#endif // _INCLUDE_SCHEDULER_H_
