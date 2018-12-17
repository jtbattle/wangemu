// This is a simple minded event scheduler.
//
// A routine desiring later notification at some specific time calls
//
//     auto tmr = TimerCreate(ticks, std::bind(&obj::fcn, &obj, arg) );
//
// which causes 'fcn' to be called back with parameter arg after simulating
// 'ticks' clock cycles.  The event is then removed from the active list.
// That is, timers are one-shots, not oscillators.  The function arg list
// can be zero, one, two, etc, arguments, just so long as bind supplies
// an argument for each parameter in the called function.
//
// A timer can be canceled early like this:
//
//     tmr->Kill();

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
// as a callback may result in a call to TimerCreate().

#include "Scheduler.h"
#include "Ui.h"         // needed for UI_Error()

#include <algorithm>    // for std::sort

// ======================================================================
// minimal scheduler test
// ======================================================================

#define TEST_TIMER 0    // if 1, set up a few timers at time 0

// set up some events and check that they trigger properly
#if TEST_TIMER
static Scheduler test_scheduler;
static void TimerTest(void);
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
        (void)test_scheduler.TimerCreate( 4, std::bind(&TimerTestFoo::report1, &g_foo2, 5) );
    }
}

void
TimerTestFoo::report2(int i)
{
    printf("report2: got callback for timer %d after %d clocks\n", i, g_testtime);
}

static void
TimerTest(void)
{
    TimerTestFoo foo;

    g_testtime = 0;

    // method 1: create a static timer object, then pass it to scheduler
    sched_callback_t cb1 = std::bind(&TimerTestFoo::report1, &foo, 1);
    auto t1 = test_scheduler.TimerCreate( TIMER_US(3.0f), cb1 );

    // method 2: like method 1, but all inline
    auto t2 = test_scheduler.TimerCreate( 10, std::bind(&TimerTestFoo::report1, &foo, 2) );
    auto t3 = test_scheduler.TimerCreate( 50, std::bind(&TimerTestFoo::report2, &foo, 3) );

    for (int n=0; n<100; n++) {
        if (n == 5) {
            t1->Kill();
        }
        test_scheduler.TimerTick(1);
    }
}
#endif

// ======================================================================
// Scheduler implementation
// ======================================================================

static const int64 MAX_TIME = (1LL << 62);

Scheduler::Scheduler() :
    m_time_ns(0),
    m_trigger_ns(MAX_TIME)
{
#if TEST_TIMER
    if (this == &test_scheduler) {
        TimerTest();
    }
#endif
};


// free allocated data
Scheduler::~Scheduler()
{
    // TODO: unnecessary? won't the vector container destroy all members?
    m_timer.clear();
};


// return a timer object; the caller doesn't destroy this object,
// but calls Kill() if it wants to terminate it.
// 'ns' is the number of nanoseconds in the future when the callback fires.
std::shared_ptr<Timer>
Scheduler::TimerCreate(int64 ns, const sched_callback_t &fcn)
{
    // catch dumb bugs
    assert(ns >= 1);
    assert(ns <= 12E9);      // 12 seconds

    // make sure we don't leak timers
    assert(m_timer.size() < MAX_TIMERS);

    int64 event_ns = m_time_ns + ns;
    auto tmr = std::make_shared<Timer>(this, event_ns, fcn);

    m_timer.push_back(tmr);
    m_trigger_ns = FirstEvent();

    // return timer handle
    return tmr;
}

int64
Scheduler::FirstEvent() noexcept
{
    int64 rv = MAX_TIME;
    for (auto &t : m_timer) {
        if (t->expires_ns < rv) {
            rv = t->expires_ns;
        }
    }
    return rv;
}

// kill a timer.  the timer number passed to this function
// is the one returned by the TimerCreate function.
// if we happen to be killing the timer nearest to completion,
// we don't bother messing with updating m_countdown.
// instead, we just let that countdown expire, nothing will
// be triggered, and a new countdown will be established then.
void Scheduler::TimerKill(Timer* tmr)
{
    // the fact that we have to do this lookup doesn't matter since
    // TimerKill is infrequently used.
    m_timer.erase(
        std::remove_if( begin(m_timer), end(m_timer),
                        [&tmr](auto q){ return (q.get() == tmr); }),
        end(m_timer));
#ifdef TMR_DEBUG
    UI_Error("Error: killing non-existent simulated timer");
#endif
}

// the m_trigger_ns threshold has been exceeded.  check all timers and invoke
// callback all those which have expired.
// this shouldn't need to be called very frequently.
void Scheduler::TimerCredit(void)
{
    if (m_timer.empty()) {
        // don't trigger this fcn again until there is real work to do
        m_trigger_ns = MAX_TIME;
        return; // no timers
    }

    // scan each active timer, moving expired ones to the retired list
    std::vector<std::shared_ptr<Timer>> retired;
    const int active_before = m_timer.size();
    int active_after = 0;
    for (int s=0; s<active_before; s++) {
        if (m_timer[s].unique()) {
            // the timer is killed because the scheduler holds the only
            // reference to it.
            ;
        } else {
            if (m_timer[s]->expires_ns <= m_time_ns) {
                // a timer has expired; move it to the retired list
                retired.push_back(m_timer[s]);
            } else {
                // this timer hasn't expired
                m_timer[active_after++] = m_timer[s];  // keep it in the active list
            }
        }
    }

    if (active_after < active_before) {
        // shrink active list, but null out pointers so resize doesn't
        // free them, as they now live on the retired list
        for (int i=active_after; i < active_before; i++) {
            m_timer[i] = nullptr;
        }
        m_timer.resize(active_after);  // delete any expired timers
    }

    // find the next event
    m_trigger_ns = FirstEvent();

    // sort retired events in order they expired
    std::sort(begin(retired), end(retired),
              [](const std::shared_ptr<Timer> &a,
                 const std::shared_ptr<Timer> &b) {
                    return (a->expires_ns < b->expires_ns);
               });

    // scan through the retired list and perform callbacks
    for (auto &t : retired) {
        (t->callback)();
    }
}


// ======================================================================
// ======================================================================

// kill off this timer
void Timer::Kill()
{
    s->TimerKill(this);
}

// vim: ts=8:et:sw=4:smarttab
