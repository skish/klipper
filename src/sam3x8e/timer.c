// SAM3x8e timer interrupt scheduling
//
// Copyright (C) 2016  Kevin O'Connor <kevin@koconnor.net>
//
// This file may be distributed under the terms of the GNU GPLv3 license.

#include "autoconf.h" // CONFIG_CLOCK_FREQ
#include "board/misc.h" // timer_from_us
#include "command.h" // shutdown
#include "irq.h" // irq_disable
#include "sam3x8e.h" // TC0
#include "sched.h" // sched_timer_kick


/****************************************************************
 * Low level timer code
 ****************************************************************/

DECL_CONSTANT(CLOCK_FREQ, CONFIG_CLOCK_FREQ);
DECL_CONSTANT(MCU, "sam3x8e");

// Return the number of clock ticks for a given number of microseconds
uint32_t
timer_from_us(uint32_t us)
{
    return us * (CONFIG_CLOCK_FREQ / 1000000);
}

// IRQ handler
void __visible
TC0_Handler(void)
{
    TC0->TC_CHANNEL[0].TC_SR; // clear irq pending
    irq_disable();
    sched_timer_kick();
    irq_enable();
}

static void
timer_set(uint32_t value)
{
    TC0->TC_CHANNEL[0].TC_RA = value;
}

static void
timer_init(void)
{
    TcChannel *tc = &TC0->TC_CHANNEL[0];
    // Reset the timer
    tc->TC_CCR = TC_CCR_CLKDIS;
    tc->TC_IDR = 0xFFFFFFFF;
    tc->TC_SR;
    // Enable it
    PMC->PMC_PCER0 = 1 << ID_TC0;
    tc->TC_CMR = TC_CMR_WAVE | TC_CMR_WAVSEL_UP | TC_CMR_TCCLKS_TIMER_CLOCK1;
    tc->TC_IER = TC_IER_CPAS;
    NVIC_SetPriority(TC0_IRQn, 1);
    NVIC_EnableIRQ(TC0_IRQn);
    timer_set(1);
    tc->TC_CCR = TC_CCR_CLKEN | TC_CCR_SWTRG;
}
DECL_INIT(timer_init);

// Called by main code once every millisecond.  (IRQs disabled.)
void
timer_periodic(void)
{
}

// Return the current time (in absolute clock ticks).
uint32_t
timer_read_time(void)
{
    return TC0->TC_CHANNEL[0].TC_CV;
}

#define TIMER_MIN_TICKS 100

// Set the next timer wake time (in absolute clock ticks).  Caller
// must disable irqs.  The caller should not schedule a time more than
// a few milliseconds in the future.
uint8_t
timer_set_next(uint32_t next)
{
    uint32_t cur = timer_read_time();
    uint32_t mintime = cur + TIMER_MIN_TICKS;
    if (sched_is_before(mintime, next)) {
        timer_set(next);
        return 0;
    }
    timer_set(mintime);
    return 1;
}

static uint32_t timer_repeat_until;
#define TIMER_IDLE_REPEAT_TICKS timer_from_us(500)
#define TIMER_REPEAT_TICKS timer_from_us(100)

#define TIMER_MIN_TRY_TICKS timer_from_us(1)
#define TIMER_DEFER_REPEAT_TICKS timer_from_us(5)

// Similar to timer_set_next(), but wait for the given time if it is
// in the near future.
uint8_t
timer_try_set_next(uint32_t next)
{
    uint32_t now = timer_read_time();
    int32_t diff = next - now;
    if (diff > (int32_t)TIMER_MIN_TRY_TICKS)
        // Schedule next timer normally.
        goto done;

    // Next timer is in the past or near future - can't reschedule to it
    if (likely(sched_is_before(now, timer_repeat_until))) {
        // Can run more timers from this irq; briefly allow irqs
        irq_enable();
        while (diff >= 0) {
            // Next timer is in the near future - wait for time to occur
            now = timer_read_time();
            diff = next - now;
        }
        irq_disable();
        return 0;
    }
    if (diff < (int32_t)(-timer_from_us(1000)))
        goto fail;

    // Too many repeat timers from a single interrupt - force a pause
    timer_repeat_until = now + TIMER_REPEAT_TICKS;
    next = now + TIMER_DEFER_REPEAT_TICKS;

done:
    timer_set(next);
    return 1;
fail:
    shutdown("Rescheduled timer in the past");
}

// Periodic background task that temporarily boosts priority of
// timers.  This helps prioritize timers when tasks are idling.
static void
timer_task(void)
{
    irq_disable();
    timer_repeat_until = timer_read_time() + TIMER_IDLE_REPEAT_TICKS;
    irq_enable();
}
DECL_TASK(timer_task);
