/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

/* Gray-code decoder: two valid quarter steps per detent (30 detents/15 pulses).
 * Reverse edges cancel bounce. Invalid two-bit jumps discard partial motion.
 */
typedef struct { unsigned previous; int quarter_steps; } keyboard_encoder_decoder_t;
static inline int keyboard_encoder_decode(keyboard_encoder_decoder_t *d, unsigned state)
{
    static const signed char delta[16] = {
        0, -1, 1, 0, 1, 0, 0, -1, -1, 0, 0, 1, 0, 1, -1, 0
    };
    const unsigned previous = d->previous;
    d->previous = state;
    if ((previous ^ state) == 3) { d->quarter_steps = 0; return 0; }
    d->quarter_steps += delta[(previous << 2) | state];
    /* Emit only at the half-cycle detents, not at a bouncing intermediate edge. */
    if (state != 0 && state != 3) { return 0; }
    int result = d->quarter_steps / 2;
    d->quarter_steps = 0;
    return result;
}
