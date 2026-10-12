/* match_state.h -- the live match, read from guest memory (runtime/core/match_state.c).
 * Layout and evidence: docs/MATCH_STATE.md. Units: metres, game seconds. Pitch frame: origin at the
 * centre spot, x across (touch lines near +-35), z along (try lines near +-50), y up. */
#ifndef MATCH_STATE_H
#define MATCH_STATE_H
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MS_TEAMS   2
#define MS_PLAYERS 15   /* per team on the field */

typedef struct {
    float pos[3];       /* live position */
    float heading;      /* degrees */
    uint8_t number;     /* shirt number 1..15 */
    uint8_t parked;     /* off the field for a cutscene (the game parks players at 95.25, 95.25) */
    uint32_t state_vt;  /* the player's AI/control state (class vtable, docs/MATCH_STATE.md) */
} ms_player_t;

typedef struct {
    float pos[3], vel[3];
    int8_t holder_team, holder; /* -1: nobody holds it (loose, in flight, at a set piece) */
} ms_ball_t;

typedef struct {
    uint16_t player_id;   /* the scorer's player id (team setup record) */
    uint32_t type;        /* 0 try, 1/2 the place-kick results, 3 the 3-point kick */
    uint32_t team_id;     /* setup team id (ms_team_id) */
    float clock;          /* game seconds */
} ms_score_ev_t;

int ms_active(void);                      /* a match object exists (CRugby) */
uint32_t ms_phase_vt(void);               /* the match state machine's current state (class vtable) */
const char *ms_phase(void);               /* its name: Kickoff, Formation, Run, Free, LineOut, ... or "?" */
float ms_clock(void);                     /* match clock, game seconds */
uint32_t ms_team_id(int team);            /* 0 home, 1 away */
int ms_player(int team, int i, ms_player_t *out);   /* 1 if that player exists */
int ms_ball(ms_ball_t *out);
int ms_score_events(ms_score_ev_t *out, int max);   /* oldest first; returns the count */
int ms_score(int team);                   /* points from the events (types 1/2 provisional, see docs) */
int ms_controlled(int team, int slot, int *index);  /* human slot 0..3 of a team: 1 if it controls player index */
uint32_t ms_setup_va(void);               /* the match setup block (static, image) */
uint32_t ms_setup_size(void);

/* match_setup.c: the bytes that define a match (rosters, kits, conditions, half length, stadium,
   difficulty), recorded and replaced at the launch step (hook "ms:launch"). */
#define MS_SETUP_BYTES (0x1908 + 4)
int ms_setup_capture(uint8_t *out);       /* MS_SETUP_BYTES into out; returns the count */
void ms_setup_offer(const uint8_t *in);   /* the next launch plays this setup instead of its own */
uint64_t ms_setup_hash(void);
void ms_launch(void);                     /* hook: FUN_004F3D10 entry */
void ms_setup_frame(uint32_t frame);      /* RT_MS_AFTER_LAUNCH (ms_frame calls it) */

void ms_frame(uint32_t frame);            /* Present hook: RT_MS_LOG=N trace line every N frames */

#ifdef __cplusplus
}
#endif
#endif
