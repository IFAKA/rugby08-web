/* det_local.c -- deterministic mode: guest work that is one player's own (a party: each game's view of
 * the match, the camera's step for this player's team). Every game must schedule its guest threads
 * alike, and the scheduler counts guest function entries (a slice every 4096, trace.c) and clock polls
 * (det.c): one player's extra work would move every later thread switch (measured: RT_DET_SCHED). Inside
 * det_local_begin .. det_local_end this thread is never preempted, its clock reads are not polls, and at
 * the end its entry count is what it was at the start, so the next switch comes where it would have
 * without the section. The section must not wait for other threads or allocate shared memory. */
#include "rt.h"

extern RECOMP_TLS uint32_t t_enter_idx;   /* trace.c: guest function entries on this thread */

RECOMP_TLS int t_det_local;   /* nesting depth (det.c, cpu.c: no preemption, no clock polls) */
static RECOMP_TLS uint32_t t_mark;

void det_local_begin(void) {
    if (!t_det_local++) t_mark = t_enter_idx;
}

void det_local_end(void) {
    if (t_det_local <= 0) return;
    t_det_local--;
    if (!t_det_local) t_enter_idx = t_mark;
}
