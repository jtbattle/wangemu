// This is a simple minded event scheduler.  A finite number of timers can be
// outstanding at any given time, which is not an issue for its intended use.
// See the corresponding .cpp for more details of its history and function.

#ifndef _INCLUDE_SCHEDULER_H_
#define _INCLUDE_SCHEDULER_H_

#include "w2200.h"  // pick up def of int32


// when a timer expires, we invoke the callback function
typedef std::function<void()> sched_callback_t;

// ======================================================================
// A Timer is just a handle that Scheduler can pass back on timer creation,
// making it more natural for the recipient to manipulate the timer later.

// fwd reference
class Scheduler;

class Timer
{
    // actually I think it would be safe, but there is no need to do this
    CANT_ASSIGN_OR_COPY_CLASS(Timer);

    friend class Scheduler;

public:
    // ticks is at what absolute time, in ns, to invoke the callback
    Timer(Scheduler *sched, int64 time_ns, sched_callback_t cb) :
        s(sched), expires_ns(time_ns), callback(cb) { };
    ~Timer() { };

    // kill off this timer
    void Kill();

private:
    Scheduler       * const s;      // pointer to owning scheduler
    int64             expires_ns;   // tick count until expiration
    sched_callback_t  callback;     // registered callback function
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
    std::shared_ptr<Timer> TimerCreate(int64 ns, const sched_callback_t &fcn);

    // let 'ns' nanoseconds of simulated time go past
    inline void TimerTick(int ns)
    {
        m_time_ns += ns;
        if (m_time_ns >= m_trigger_ns) {
            TimerCredit();
        }
    }

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

    // returns, in absolute ns, the time of the soonest event on the timer list
    int64 FirstEvent() noexcept;

    int64 m_time_ns;        // simulated absolute time (in ns)
    int64 m_trigger_ns;     // time next event expires

    // list of callbacks to invoke when m_time_ns exceeds the expiration time
    // embedded in the timer.
    std::vector<std::shared_ptr<Timer>> m_timer;

    // not strictly necesssary to place a limit, but it is useful to
    // detect runaway conditions
    static const int MAX_TIMERS = 30;
};

// scale us/ms to ns, which is what TimerCreate() expects
constexpr int64 TIMER_US(double f) { return static_cast<int64>(   1000.0*f+0.5); }
constexpr int64 TIMER_MS(double f) { return static_cast<int64>(1000000.0*f+0.5); }

#endif // _INCLUDE_SCHEDULER_H_

// vim: ts=8:et:sw=4:smarttab
