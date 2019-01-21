// This is a simple minded event scheduler.
//
// A routine desiring later notification at some specific time calls
//
//     auto tmr = createTimer(ticks, std::bind(&obj::fcn, &obj, arg));
//
// which causes 'fcn' to be called back with parameter arg after simulating
// 'ticks' clock cycles.  The event is then removed from the active list.
// That is, timers are one-shots, not oscillators.  The function arg list
// can be zero, one, two, etc, arguments, just so long as bind supplies
// an argument for each parameter in the called function.
//
// A timer can be canceled early simply by setting it to nullptr.

// History:
//    2000-2001: originally developed Solace, a sol-20 emulator for win32
//         2003: modified for wangemu,  a wang 2200 emulator
//         2007: rewritten in c++ oo style for the wang 3300 emulator
//         2008: adapted for a new revision of the wang 2200 emulator
//         2015: replaced Callback.h with std::function/std::bind
//         2018: switched to a ns resolution, 64b absolute time model
// All revisions, Jim Battle.

// When m_time_ns has incremented past the threashold of the earliest timer,
// all timers are checked as more than one might expire. All expiring timers
// are put on a retirement list, then all retired timers perform their
// callbacks. This retirement list is to prevent confusing reentrancy issues,
// as a callback may result in a call to createTimer().

#include "Scheduler.h"
#include "Ui.h"         // needed for UI_error()

#include <algorithm>    // for std::sort

// ======================================================================
// minimal scheduler test
// ======================================================================

#define TEST_TIMER 0    // if 1, set up a few timers at time 0

// set up some events and check that they trigger properly
#if TEST_TIMER
static Scheduler test_scheduler;
static void timerTest(void);
static int g_testtime;

class TimerTestFoo
{
public:
    TimerTestFoo() {};
    ~TimerTestFoo() {};

    void report1(int i);
    void report2(int i);
};

TimerTestFoo g_foo2;


void
TimerTestFoo::report1(int i)
{
    printf("report1: got callback for timer %d after %d clocks\n", i, g_testtime);
    if (i == 2) {
        (void)test_scheduler.createTimer(4, std::bind(&TimerTestFoo::report1, &g_foo2, 5));
    }
}


void
TimerTestFoo::report2(int i)
{
    printf("report2: got callback for timer %d after %d clocks\n", i, g_testtime);
}


static void
timerTest(void)
{
    TimerTestFoo foo;

    g_testtime = 0;

    // method 1: create a static timer object, then pass it to scheduler
    sched_callback_t cb1 = std::bind(&TimerTestFoo::report1, &foo, 1);
    auto t1 = test_scheduler.createTimer(TIMER_US(3.0f), cb1);

    // method 2: like method 1, but all inline
    auto t2 = test_scheduler.createTimer(10, std::bind(&TimerTestFoo::report1, &foo, 2));
    auto t3 = test_scheduler.createTimer(50, std::bind(&TimerTestFoo::report2, &foo, 3));

    for (int n=0; n < 100; n++) {
        if (n == 5) {
            t1 = nullptr;
        }
        test_scheduler.timerTick(1);
    }
}
#endif

// ======================================================================
// Scheduler implementation
// ======================================================================

Scheduler::Scheduler()
{
#if TEST_TIMER
    if (this == &test_scheduler) {
        timerTest();
    }
#endif
};


// return a timer object; the caller doesn't destroy this object,
// but sets it to nullptr when it is done with it (early or not).
// 'ns' is the number of nanoseconds in the future when the callback fires.
std::shared_ptr<Timer>
Scheduler::createTimer(int64 ns, const sched_callback_t &fcn)
{
    // catch dumb bugs
    assert(ns >= 1);
    assert(ns <= 12E9);      // 12 seconds

    // make sure we don't leak timers
    // the one tricky case that pushed the limit above 30 (max 37 seen)
    // is the SNAKE220 game on the "more_games.wvd" disk. for whatever
    // reason, the way it is written causes a lot of blocking events that
    // cause the 27ms time slice one-shot to be retriggered frequently.
    // each touch creates a new event on the callback queue, but the zombie
    // ones don't retired until their own 27ms window completes. eventually
    // that happens, but there can be me as many zombies as the scheduler
    // touches the one-shot in a given 27ms window.
#if 1
    static unsigned int max_timers = MAX_TIMERS;
    if (m_timer.size() > max_timers) {
        max_timers = m_timer.size();
        UI_warn("now at %d timers", max_timers);
    }
#else
    assert(m_timer.size() < MAX_TIMERS);
#endif

    int64 event_ns = m_time_ns + ns;
    auto tmr = std::make_shared<Timer>(event_ns, fcn);

    m_timer.push_back(tmr);
    m_trigger_ns = firstEvent();

    // return timer handle
    return tmr;
}


int64
Scheduler::firstEvent() noexcept
{
    int64 rv = MAX_TIME;
    for (auto &t : m_timer) {
        if (t->m_expires_ns < rv) {
            rv = t->m_expires_ns;
        }
    }
    return rv;
}


// the m_trigger_ns threshold has been exceeded.  check all timers and invoke
// callback all those which have expired.
// this shouldn't need to be called very frequently.
void Scheduler::creditTimer()
{
    if (m_timer.empty()) {
        // don't trigger this fcn again until there is real work to do
        m_trigger_ns = MAX_TIME;
        return; // no timers
    }

    // scan each active timer, moving expired ones to the retired list.
    std::vector<std::shared_ptr<Timer>> retired;
    const int active_before = m_timer.size();
    int active_after = 0;
    for (int s=0; s < active_before; s++) {
        if (m_timer[s].unique()) {
            // the timer is dead: the scheduler holds the only reference to it
            ;
        } else {
            if (m_timer[s]->m_expires_ns <= m_time_ns) {
                // a timer has expired; move it to the retired list
                retired.push_back(m_timer[s]);
            } else {
                // this timer hasn't expired
                m_timer[active_after++] = m_timer[s];  // keep it in the active list
            }
        }
    }

    // delete any expired timers
    if (active_after < active_before) {
        // shrink active list, but null out pointers so resize doesn't
        // free them, as they now live on the retired list
        for (int i=active_after; i < active_before; i++) {
            m_timer[i] = nullptr;
        }
        m_timer.resize(active_after);
    }

    // find the next event
    m_trigger_ns = firstEvent();

    // sort retired events in order they expire
    std::sort(begin(retired), end(retired),
              [](const std::shared_ptr<Timer> &a,
                 const std::shared_ptr<Timer> &b) {
                    return (a->m_expires_ns < b->m_expires_ns);
               });

    // scan through the retired list and perform callbacks
    for (auto &t : retired) {
        (t->m_callback)();
    }
}

// vim: ts=8:et:sw=4:smarttab
