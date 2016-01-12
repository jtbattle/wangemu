// This is a simple minded event scheduler.
//
// A routine desiring later notification at some specific time calls
//
//     Timer *tmr = TimerCreate(ticks, std::bind(&obj::fcn, &obj, arg) );
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
// All revisions, Jim Battle.

// In theory, on each TimerTick() we deduct the specified number of ticks
// from all the timers.  All expiring timers are put on a retirement list.
// Once all active timers are swept like this, all retired timers perform
// their callbacks.  This retirement list is to prevent confusing reentrancy
// issues, as a callback may result in a call to TimerCreate().
//
// In practice, we just keep track of the time left on the timer with the
// fewest ticks remaining.  once it retires (or dies), only then are all
// the other outstanding timers debited the cumulative tick count since the
// last such accounting.
//
// The timer is only a 32 bit integer, but only 30 bits should be used as
// things might get squirrelly near 2^31.  At the nominal 625 KHz base
// frequency (1.6 uS cycle time), this allows setting a timer event for
// about 57 minutes into the future.  This is more than enough range.

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
    if (i == 2)
        (void)test_scheduler.TimerCreate( 4, std::bind(&TimerTestFoo::report1, &g_foo2, 5) );
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
    callback_t cb1 = std::bind(&TimerTestFoo::report1, &foo, 1);
    Timer *t1 = test_scheduler.TimerCreate( TIMER_US(3.0f), cb1 );

    // method 2: like method 1, but all inline
    Timer *t2 = test_scheduler.TimerCreate( 10, std::bind(&TimerTestFoo::report1, &foo, 2) );
    Timer *t3 = test_scheduler.TimerCreate( 50, std::bind(&TimerTestFoo::report2, &foo, 3) );

    for(int n=0; n<100; n++) {
        if (n == 5)
            t1->Kill();
        test_scheduler.TimerTick(1);
    }
}
#endif

// ======================================================================
// Scheduler implementation
// ======================================================================

Scheduler::Scheduler() :
    m_updating(false),
    m_countdown(0),
    m_startcnt(0)
{
#if TEST_TIMER
    if (this == &test_scheduler)
        TimerTest();
#endif
};


// free allocated data
Scheduler::~Scheduler()
{
// FIXME: use smart pointers so I don't need to explicitly delete dead timers
//        (here and TimerKill and from the retired list in TimerCredit)
    for(auto t : m_timer) {
        delete t;
    }
    m_timer.clear();
};


// return a timer object; the caller doesn't destroy this object,
// but calls Kill() if it wants to terminate it.
// 'ticks' is the number of clock ticks before the callback fires.
Timer*
Scheduler::TimerCreate(int ticks, const callback_t &fcn)
{
    // funny things happen if we try to time intervals that are too big
    assert(ticks <= MAX_TICKS);
    assert(ticks >= 1);

    // make sure we don't leak timers
    assert(m_timer.size() < MAX_TIMERS);

    Timer *tmr = new Timer(this, ticks, fcn);

    if (m_timer.size() == 1) {
        // this is the only timer
        m_startcnt = m_countdown = ticks;
    } else if (ticks < m_countdown) {
        // the new timer expires before current target, so make this timer
        // the new expiration count.  we have to twiddle startcnt to make
        // sure we end up with the proper timer credits for those that
        // have already been running.
        int32 diff = (m_countdown - ticks);
        m_startcnt  -= diff;
        m_countdown -= diff;    // == ticks
    } else {
        // the timer state might be that we already have credits lined up
        // for other timers.  rather than doling out the elapsed time to
        // each timer, we just add the current credit to the new timer.
        // that is, if N cycles have gone by since the last time we
        // evaluated who to trigger next, we pretend that the new event
        // which is to trigger in M cycles was actually scheduled for M+N
        // ticks N cycles ago.
        int elapsed = (m_startcnt - m_countdown);
        tmr->ctr += elapsed;
    }

    m_timer.push_back(tmr);

    // return timer handle
    return tmr;
}

// kill a timer.  the timer number passed to this function
// is the one returned by the TimerCreate function.
// if we happen to be killing the timer nearest to completion,
// we don't bother messing with updating m_countdown.
// instead, we just let that countdown expire, nothing will
// be triggered, and a new countdown will be established then.
void Scheduler::TimerKill(Timer *tmr)
{
    // the fact that we have to do this lookup doesn't matter since
    // TimerKill is infrequently used.
    for(unsigned int n=0; n<m_timer.size(); n++) {
        if (m_timer[n] == tmr) {
            m_timer.erase(m_timer.begin() + n);
            delete tmr;
            return;
        }
    }
#ifdef TMR_DEBUG
    UI_Error("Error: killing non-existent simulated timer");
#endif
}

// transfer accumulated timer deficit to each active timer.
// n is added to the already elapsed time.
// this shouldn't need to be called very frequently.
void Scheduler::TimerCredit(void)
{
    if (m_timer.empty()) {
        // don't trigger this fcn again until there is real work to do,
        // or a long time has passed.
        m_countdown = MAX_TICKS;
        return; // no timers
    }

    uint32 elapsed = (m_startcnt - m_countdown);

    // we set this flag because the loop below may end up calling one
    // or more callbacks which themselves might call TimerCreate(), and
    // this flag is used in that routine to avoid a reentrancy issue.
    m_updating = true;

    // scan each active timer, moving expired ones to the retired list
    vector<Timer *> retired;
    unsigned int active_before = m_timer.size();
    unsigned int active_after = 0;
    for(unsigned int s=0; s<active_before; s++) {
        m_timer[s]->ctr -= elapsed;
        if (m_timer[s]->ctr <= 0) {
            // a timer has expired; move it to the retired list
            retired.push_back(m_timer[s]);
        } else {
            // this timer hasn't expired
            m_timer[active_after++] = m_timer[s];  // keep it in the active list
        }
    }
    if (active_after < active_before) {
        // shrink active list, but null out pointers so resize doesn't
        // free them, as they now live on the retired list
        for(unsigned int i=active_after; i < active_before; i++) {
            m_timer[i] = nullptr;
        }
        m_timer.resize(active_after);  // delete any expired timers
    }

    // determine the minimum countdown value of the remaining active timers
    m_countdown = MAX_TICKS;
    for(auto t : m_timer) {
        m_countdown = std::min(m_countdown, t->ctr);
    }
    m_startcnt = m_countdown;

    // sort retired events in order they expired
    std::sort(retired.begin(), retired.end(),
              [](Timer *a, Timer *b) { return (a->ctr < b->ctr); }
             );
    // scan through the retired list and perform callbacks
    for(auto t : retired) {
        (t->callback)();
        delete t;
    }

    m_updating = false; // done updating
}


// this is called periodically to advance time

#if 0
// let 'n' cpu cycles of simulated time go past
void Scheduler::TimerTick(int n)
{
#if TEST_TIMER
    if (this == &test_scheduler) {
        g_testtime += n;
        if (g_testtime < 100)
            printf("inc %d, time=%d\n", n, g_testtime);
    }
#endif
    assert(!m_updating);

    m_countdown -= n;
    if (m_countdown <= 0) {

        // it is likely that one or more timers has expired.
        // it isn't guaranteed since it is possible that the timer
        // which was nearest to completion was recently TimerKill'd.
        TimerCredit();

    } // else no active timers
}
#endif

// ======================================================================
// ======================================================================

// kill off this timer
void Timer::Kill()
{
    s->TimerKill(this);
}

// vim: ts=8:et:sw=4:smarttab
