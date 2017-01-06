// This is a simple minded event scheduler.  A finite number of timers can be
// outstanding at any given time, which is not an issue for its intended use.
// See the corresponding .cpp for more details of its history and function.

#ifndef _INCLUDE_SCHEDULER_H_
#define _INCLUDE_SCHEDULER_H_

#include "w2200.h"  // pick up def of int32


// when a timer expires, we invoke the callback function
typedef std::function<void()> sched_callback_t;

// ======================================================================
// This is just a handle that Scheduler can pass back on timer creation,
// making it more natural for the recipient to manipulate the timer later.

// fwd reference
class Scheduler;

class Timer
{
    // actually I think it would be safe, but there is no need to do this
    CANT_ASSIGN_OR_COPY_CLASS(Timer);

    friend class Scheduler;

public:
    Timer(Scheduler *sched, int ticks, sched_callback_t cb) :
        s(sched), ctr(ticks), callback(cb) { };
    ~Timer() { };

    // kill off this timer
    void Kill();

private:
    Scheduler       * const s;  // pointer to owning scheduler
    int32             ctr;      // tick count until expiration
    sched_callback_t  callback; // registered callback function
};

// ======================================================================
// this class manages event-driven behavior for the emulator.
// time advances every cpu tick, and callers can request to be called
// back with a specific parameter after some number of cycles.
// timers can also be killed before they have come due.

class Scheduler
{
    friend class Timer;  // so timer can see TimerKill()

public:
     Scheduler();
    ~Scheduler();

    // create a new timer
    // ticks is the number of clock ticks before the callback fires,
    // passing back the stored arg. e.g.,
    //
    //   void TimerTestFoo::report(int i)
    //   { printf("got callback for timer %d after %d clocks\n", i, g_testtime); }
    //
    //   auto tmr = TimerCreate( 100,
    //                           std::bind(&TimerTestFoo:report, &foo, 33) );
    //
    // After 100 clocks, foo.report(33) is called.
    std::shared_ptr<Timer> TimerCreate(int ticks, const sched_callback_t &fcn);

    // let 'n' cpu cycles of simulated time go past
    inline void TimerTick(int n)
    {
        m_countdown -= n;
        if (m_countdown <= 0) {
            TimerCredit();
        }
    }

    // when computing time deltas, we don't want to get confused
    // my negative numbers and wrapping
    const static int MAX_TICKS = (1 << 30)-1;

private:
    // transfer accumulated timer deficit to each active timer.
    // n is added to the already elapsed time.
    // this shouldn't need to be called very frequently.
    void TimerCredit();

    // remove a pending timer event by passing the timer object
    // (called only from Timer.Kill())
    // Alternately, just set the timer pointer to null.  If the only ref
    // to the timer is the active timer list, it will be treated as dead.
    void TimerKill(Timer* tmr);

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
    vector<std::shared_ptr<Timer>> m_timer;

    // for sanity checking
    static const int MAX_TIMERS = 25;
};

// scale a floating point time in microseconds to be an
// argument appropriate for the TimerCreate() function.
inline int TIMER_US(double f) { return int(   10.0*f+0.5); }
inline int TIMER_MS(double f) { return int(10000.0*f+0.5); }

#endif // _INCLUDE_SCHEDULER_H_

// vim: ts=8:et:sw=4:smarttab
