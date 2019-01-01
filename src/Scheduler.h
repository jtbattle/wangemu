// This is a simple minded event scheduler.  A finite number of timers can be
// outstanding at any given time, which is not an issue for its intended use.
// See the corresponding .cpp for more details of its history and function.

#ifndef _INCLUDE_SCHEDULER_H_
#define _INCLUDE_SCHEDULER_H_

#include "w2200.h"  // pick up def of int32


// when a timer expires, we invoke the callback function
using sched_callback_t = std::function<void()>;

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
    Timer(int64 time_ns, sched_callback_t cb) :
            m_expires_ns(time_ns), m_callback(cb) { };

private:
    int64             m_expires_ns; // tick count until expiration
    sched_callback_t  m_callback;   // registered callback function
};


// ======================================================================
// this class manages event-driven behavior for the emulator.
// time advances every cpu tick, and callers can request to be called
// back with a specific parameter after some number of cycles.
// timers can also be killed before they have come due.

class Scheduler
{
    friend class Timer;  // so timer can see killTimer()

public:
     Scheduler();

    // create a new timer
    // ticks is the number of clock ticks before the callback fires,
    // passing back the stored arg. e.g.,
    //
    //   void TimerTestFoo::report(int i)
    //   { printf("got callback for timer %d after %d clocks\n", i, g_testtime); }
    //
    //   auto tmr = createTimer(100,
    //                          std::bind(&TimerTestFoo:report, &foo, 33));
    //
    // After 100 clocks, foo.report(33) is called.
    std::shared_ptr<Timer> createTimer(int64 ns, const sched_callback_t &fcn);

    // let 'ns' nanoseconds of simulated time go past
    inline void timerTick(int ns)
    {
        m_time_ns += ns;
        if (m_time_ns >= m_trigger_ns) {
            creditTimer();
        }
    }

private:
    // not strictly necesssary to place a limit, but it is useful to
    // detect runaway conditions
    static const int MAX_TIMERS = 30;

    // things get hinky if we get near the sign bit, but that isn't
    // a concern anymore now that we're using int64.
    static const int64 MAX_TIME = (1LL << 62);

    // transfer accumulated timer deficit to each active timer.
    // n is added to the already elapsed time.
    // this shouldn't need to be called very frequently.
    void creditTimer();

    // returns, in absolute ns, the time of the soonest event on the timer list
    int64 firstEvent() noexcept;

    int64 m_time_ns    = 0LL;       // simulated absolute time (in ns)
    int64 m_trigger_ns = MAX_TIME;  // time next event expires

    // list of callbacks to invoke when m_time_ns exceeds the expiration time
    // embedded in the timer.
    std::vector<std::shared_ptr<Timer>> m_timer;
};

// scale us/ms to ns, which is what createTimer() expects
constexpr int64 TIMER_US(double f) { return static_cast<int64>(   1000.0*f+0.5); }
constexpr int64 TIMER_MS(double f) { return static_cast<int64>(1000000.0*f+0.5); }

#endif // _INCLUDE_SCHEDULER_H_

// vim: ts=8:et:sw=4:smarttab
