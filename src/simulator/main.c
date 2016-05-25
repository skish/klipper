// Main starting point for host simulator.
//
// Copyright (C) 2016  Kevin O'Connor <kevin@koconnor.net>
//
// This file may be distributed under the terms of the GNU GPLv3 license.

#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include "sched.h" // sched_main

uint8_t Interrupt_off;


/****************************************************************
 * Timers
 ****************************************************************/

uint32_t
timer_from_ms(uint32_t ms)
{
    return 0; // XXX
}

void
timer_periodic(void)
{
}

uint32_t
timer_read_time(void)
{
    return 0; // XXX
}

uint8_t
timer_set_next(uint32_t next)
{
    return 0;
}

uint8_t
timer_try_set_next(uint32_t next)
{
    return 1;
}


/****************************************************************
 * Turn stdin/stdout into serial console
 ****************************************************************/

// XXX
char *
console_get_input(uint8_t *plen)
{
    *plen = 0;
    return NULL;
}

void
console_pop_input(uint8_t len)
{
}

// Return an output buffer that the caller may fill with transmit messages
char *
console_get_output(uint8_t len)
{
    return NULL;
}

// Accept the given number of bytes added to the transmit buffer
void
console_push_output(uint8_t len)
{
}


/****************************************************************
 * Startup
 ****************************************************************/

// Periodically sleep so we don't consume all CPU
static void
simu_pause(void)
{
    // XXX - should check that no timers are present.
    usleep(1);
}
DECL_TASK(simu_pause);

// Main entry point for simulator.
int
main(void)
{
    // Make stdin non-blocking
    fcntl(STDIN_FILENO, F_SETFL, fcntl(STDIN_FILENO, F_GETFL, 0) | O_NONBLOCK);

    sched_main();
    return 0;
}