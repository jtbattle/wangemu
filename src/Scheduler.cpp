// This is a simple minded event scheduler.
//
// A routine desiring later notification at some specific time calls
//
//     Timer *tmr = TimerCreate(ticks, obj, &obj::fcn, arg);
//
// which causes 'fcn' to be called back with parameter arg after simulating
// 'ticks' clock cycles.  The event is then removed from the active list.
// That is, timers are one-shots, not oscillators.
//
// A timer can be cancelled early like this:
//
//     tmr->Kill();

// History:
//    2000-2001: originally developed Solace, a sol-20 emulator for win32
//         2003: modified for wangemu,  a wang 2200 emulator
//         2007: rewritten in c++ oo style for the wang 3300 emulator
//         2008: adapted for a new revision of the wang 2200 emulator
// All revisions, Jim Battle.

// The code perhaps attempts to be too efficient in some ways, while no doubt
// being inefficient in others.  A fixed pool of timers are pre-manufactured
// and kept on free and in-use lists for quick allocation.
//
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
#include "Ui.h"         // needed for UI_Alert()


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
        (void)test_scheduler.TimerCreate( 4, make_callback(g_foo2, &TimerTestFoo::report1, 5) );
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
    Callback<TimerTestFoo, int> cb1 = make_callback(foo, &TimerTestFoo::report1, 1);
    Timer *t1 = test_scheduler.TimerCreate( TIMER_US(3.0f), cb1 );

    // method 2: like method 1, but all inline
    Timer *t2 = test_scheduler.TimerCreate( 10, make_callback(foo, &TimerTestFoo::report1, 2) );
    Timer *t3 = test_scheduler.TimerCreate( 50, make_callback(foo, &TimerTestFoo::report2, 3) );

    // method 3: use different TimerCreate signature to avoid make_callback() function
    Timer *t4 = test_scheduler.TimerCreate( 10, foo, &TimerTestFoo::report2, 4);

    for(int n=0; n<100; n++) {
        if (n == 5)
            t1->Kill();
        test_scheduler.TimerTick(1);
    }
}
#endif

// ======================================================================
// virtual destructor  has to live somewhere; here it is
// ======================================================================

CallbackBase::~CallbackBase() { }

// ======================================================================
// Scheduler implementation
// ======================================================================

Scheduler::Scheduler() :
    m_updating(false),
    m_countdown(0),
    m_startcnt(0),
    m_numFree(NUM_TIMERS),
    m_numActive(0),
    m_numRetired(0)
{
    // establish free list
    for(int i=0; i<NUM_TIMERS; i++)
        m_freeIdx[i] = i;

    // callback list
    for(int j=0; j<NUM_TIMERS; j++)
        m_timer[j].callback = NULL;

    // prefab timer handles
    for(int k=0; k<NUM_TIMERS; k++)
        m_timer[k].ptmr = new Timer(this, k);

#if TEST_TIMER
    if (this == &test_scheduler)
        TimerTest();
#endif
};


// free allocated data
Scheduler::~Scheduler()
{
    // kill any active timers
    for(int i=0; i < NUM_TIMERS; i++) {
        if (m_timer[i].callback != NULL)
            delete m_timer[i].callback;
        m_timer[i].callback = NULL;
    }

    // delete dynamic storage
    for(int j=0; j < NUM_TIMERS; j++)
        delete m_timer[j].ptmr;
};


// create a new timer; an int handle is returned.
// this handle can be used to identify the timer later.
// ticks is the number of clock ticks before the callback fires,
// passing back the stored arg.
Timer* Scheduler::TimerCreateImpl(int ticks, CallbackBase *fcn)
{
    // funny things happen if we try to time intervals that are too big
    assert(ticks <= MAX_TICKS);
    assert(ticks >= 1);

    // find an available slot
    if (m_numFree == 0) {
        UI_Error("Error: ran out of simulated timers");
        exit(-1);
    }

    // move timer from free list to active list
    int tmr = m_freeIdx[--m_numFree];
    m_activeIdx[m_numActive++] = tmr;

    m_timer[tmr].ctr      = ticks;  // clocks until expiration
    m_timer[tmr].callback = fcn;    // pointer to the copy we created

    if (m_numActive == 1) {
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
        m_timer[tmr].ctr += elapsed;
    }

    // timer handle
    return m_timer[tmr].ptmr;
}

// remove a pending timer event by passing its handle

// kill a timer.  the timer number passed to this function
// is the one returned by the TimerCreate function.
// if we happen to be killing the timer nearest to completion,
// we don't bother messing with updating m_countdown.
// instead, we just let that countdown expire, nothing will
// be triggered, and a new countdown will be established then.
void Scheduler::TimerKill(int n)
{
    // free the callback thunk we allocated
    if (m_timer[n].callback != NULL) {
        delete m_timer[n].callback;    // pointer to the copy we created
        m_timer[n].callback = NULL;
    }

    // the fact that we have to do this lookup doesn't matter since
    // TimerKill isn't used very frequently.
    int idx = -1;
    for(int i=0; i<m_numActive; i++) {
        if (m_activeIdx[i] == n) {
            idx = i;
            break;
        }
    }
    if (idx < 0) {
#ifdef TMR_DEBUG
        UI_Error("Error: killing non-existent simulated timer");
#endif
        return;
    }

    // add index to free list
    m_freeIdx[m_numFree++] = n;

    // remove it from the active list
    for(int j=idx; j<m_numActive-1; j++)
        m_activeIdx[j] = m_activeIdx[j+1];
    m_numActive--;
}

// transfer accumulated timer deficit to each active timer.
// n is added to the already elapsed time.
// this shouldn't need to be called very frequently.
void Scheduler::TimerCredit(void)
{
    if (m_numActive == 0) {
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

    // scan each active timer, moving expired ones to the dead list
    m_numRetired = 0;
    int i, j;
    for(i=j=0; i<m_numActive; i++) {
        int idx = m_activeIdx[i];
        int overshoot = (elapsed - m_timer[idx].ctr);
        if (overshoot >= 0) {
            // a timer has expired; add it to the retired list
            m_retiredIdx[m_numRetired++] = idx;
        } else {
            // this timer hasn't expired
            m_timer[idx].ctr -= elapsed;
            m_activeIdx[j] = idx;
            j++;
        }
    }
    m_numActive -= m_numRetired;

    // determine the new countdown target time
    m_countdown = MAX_TICKS;
    for(i=0; i<m_numActive; i++) {
        int idx = m_activeIdx[i];
        if ((i==0) || (m_timer[idx].ctr < m_countdown))
            m_countdown = m_timer[idx].ctr;
    }
    m_startcnt = m_countdown;

    // scan through the retired list and perform callbacks
    for(i=0; i<m_numRetired; i++) {
        int idx = m_retiredIdx[i];

        if (m_timer[idx].callback == NULL) {
#ifdef TMR_DEBUG
            UI_Error("Error: killing non-existent simulated timer");
#endif
        } else {
            // ideally, we'd also pass the number of base ticks of overshoot
            // so that a periodic timer could make sure the new period for
            // the next cycle accounted for this small (?) difference.
            (*m_timer[idx].callback)();

            delete m_timer[idx].callback;
            m_timer[idx].callback = NULL;
        }

        // move it to the free list
        assert(m_numFree < NUM_TIMERS);
        m_freeIdx[m_numFree++] = idx;
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
void Timer::Kill() { s->TimerKill(idx); }

