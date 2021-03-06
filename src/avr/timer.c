// AVR timer interrupt scheduling code.
//
// Copyright (C) 2016  Kevin O'Connor <kevin@koconnor.net>
//
// This file may be distributed under the terms of the GNU GPLv3 license.

#include <avr/interrupt.h> // TCNT1
#include "autoconf.h" // CONFIG_AVR_CLKPR
#include "board/misc.h" // timer_from_us
#include "command.h" // shutdown
#include "irq.h" // irq_save
#include "sched.h" // sched_timer_kick


/****************************************************************
 * Low level timer code
 ****************************************************************/

DECL_CONSTANT(CLOCK_FREQ, F_CPU);
DECL_CONSTANT(MCU, CONFIG_MCU);

// Return the number of clock ticks for a given number of microseconds
uint32_t
timer_from_us(uint32_t us)
{
    return us * (F_CPU / 1000000);
}

static inline uint16_t
timer_get(void)
{
    return TCNT1;
}

static inline void
timer_set(uint16_t next)
{
    OCR1A = next;
}

static inline void
timer_set_clear(uint16_t next)
{
    OCR1A = next;
    TIFR1 = 1<<OCF1A;
}

static inline void
timer_repeat_set(uint16_t next)
{
    // Timer1B is used to limit the number of timers run from a timer1A irq
    OCR1B = next;
    TIFR1 = 1<<OCF1B;
}

ISR(TIMER1_COMPA_vect)
{
    sched_timer_kick();
}

static void
timer_init(void)
{
    if (CONFIG_AVR_CLKPR != -1 && (uint8_t)CONFIG_AVR_CLKPR != CLKPR) {
        // Program the clock prescaler
        irqstatus_t flag = irq_save();
        CLKPR = 0x80;
        CLKPR = CONFIG_AVR_CLKPR;
        irq_restore(flag);
    }

    // no outputs
    TCCR1A = 0;
    // Normal Mode
    TCCR1B = 1<<CS10;
    // enable interrupt
    TIMSK1 = 1<<OCIE1A;
}
DECL_INIT(timer_init);


/****************************************************************
 * 32bit timer wrappers
 ****************************************************************/

static uint32_t timer_last;

// Return the 32bit current time given the 16bit current time.
static __always_inline uint32_t
calc_time(uint32_t last, uint16_t cur)
{
    union u32_u16_u {
        struct { uint16_t lo, hi; };
        uint32_t val;
    } calc;
    calc.val = last;
    if (cur < calc.lo)
        calc.hi++;
    calc.lo = cur;
    return calc.val;
}

// Called by main code once every millisecond.  (IRQs disabled.)
void
timer_periodic(void)
{
    timer_last = calc_time(timer_last, timer_get());
}

// Return the current time (in absolute clock ticks).
uint32_t
timer_read_time(void)
{
    irqstatus_t flag = irq_save();
    uint16_t cur = timer_get();
    uint32_t last = timer_last;
    irq_restore(flag);
    return calc_time(last, cur);
}

#define TIMER_MIN_TICKS 100

// Set the next timer wake time (in absolute clock ticks).  Caller
// must disable irqs.  The caller should not schedule a time more than
// a few milliseconds in the future.
uint8_t
timer_set_next(uint32_t next)
{
    uint16_t cur = timer_get();
    if ((int16_t)(OCR1A - cur) < 0 && !(TIFR1 & (1<<OCF1A)))
        // Already processing timer irqs
        try_shutdown("timer_set_next called during timer dispatch");
    uint32_t mintime = calc_time(timer_last, cur + TIMER_MIN_TICKS);
    if (sched_is_before(mintime, next)) {
        timer_set_clear(next);
        return 0;
    }
    timer_set_clear(mintime);
    return 1;
}

#define TIMER_IDLE_REPEAT_TICKS 8000
#define TIMER_REPEAT_TICKS 3000

#define TIMER_MIN_TRY_TICKS 60 // 40 ticks to exit irq; 20 ticks of progress
#define TIMER_DEFER_REPEAT_TICKS 200

// Similar to timer_set_next(), but wait for the given time if it is
// in the near future.
uint8_t
timer_try_set_next(uint32_t target)
{
    uint16_t next = target, now = timer_get();
    int16_t diff = next - now;
    if (diff > TIMER_MIN_TRY_TICKS)
        // Schedule next timer normally.
        goto done;

    // Next timer is in the past or near future - can't reschedule to it
    if (!(TIFR1 & (1<<OCF1B))) {
        // Can run more timers from this irq; briefly allow irqs
        irq_enable();
        asm("nop");
        irq_disable();

        while (diff >= 0) {
            // Next timer is in the near future - wait for time to occur
            now = timer_get();
            irq_enable();
            diff = next - now;
            irq_disable();
        }
        return 0;
    }
    if (diff < (int16_t)(-timer_from_us(1000)))
        goto fail;

    // Too many repeat timers - force a pause so tasks aren't starved
    timer_repeat_set(now + TIMER_REPEAT_TICKS);
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
    timer_repeat_set(timer_get() + TIMER_IDLE_REPEAT_TICKS);
    irq_enable();
}
DECL_TASK(timer_task);
