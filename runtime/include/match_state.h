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
int ms_phase_id(void);                    /* its id (Free 7, Kickoff 8, ...): FUN_0062DA80 changes to it */
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
   difficulty, kits), recorded and replaced at the launch step (hook "ms:launch"). */
#define MS_SETUP_BYTES (0x1908 + 4 + 1)
int ms_setup_capture(uint8_t *out);       /* MS_SETUP_BYTES into out; returns the count */
void ms_setup_offer(const uint8_t *in);   /* the next launch plays this setup instead of its own */
void ms_setup_last(uint8_t *out);         /* the setup of the last launch (MS_SETUP_BYTES) */
uint64_t ms_setup_hash(void);
void ms_launch(void);                     /* hook: FUN_004F3D10 entry */
void ms_setup_frame(uint32_t frame);      /* RT_MS_AFTER_LAUNCH (ms_frame calls it) */

/* match_replay.c: the game's replay record stream (PlayBack::CReplayManager) */
uint32_t ms_replay_manager(void);
int ms_replay_stream(uint32_t *start, uint32_t *end);   /* written records [start, end) */
void ms_replay_decode(uint32_t frame, uint32_t rec);    /* the game's decoder: frame += record */
uint32_t ms_replay_current(void);                       /* the frame recorded last (0x118C bytes), or 0 */
void ms_replay_frame(uint32_t frame);                   /* RT_MS_REPLAY_CHECK (ms_frame calls it) */

/* match_puppet.c: a friend's game showing the host's match */
int ms_puppet_on(void);
int ms_puppet_skip_update(void);
void ms_puppet_after_update(uint32_t rugby);           /* game/overrides/match/rugby_update.c */
int ms_puppet_pose_begin(uint32_t k);                  /* game/overrides/match/pose_player.c */
void ms_puppet_pose_end(void);
int ms_puppet_forcing(void);
int ms_puppet_check(char *out, int size);              /* friend's recorded frame vs the host's (RT_MS_POS) */
void ms_puppet_place(uint32_t rugby);
/* match_controls.c: the humans' players (each team's 4 player controls: the shirt number of its player) */
#define MS_CONTROLS_BYTES 8
void ms_controls_last(uint8_t *out);           /* host: its controls now (home 4, away 4; 0 none) */
void ms_controls_host(const uint8_t *shirts);  /* friend: the host's, for ms_puppet_controls */
void ms_puppet_controls(void);                 /* friend: its controls and their rings as the host's */
/* players and ball, after the friend's presentation */ /* game/overrides/match/replay_playing.c */
uint32_t ms_player_va(int team, int i); /* CRugbyPlayer* or 0 */
uint32_t ms_team_va(int team); /* CRugbyTeam* or 0 */
int ms_team_of(uint32_t va, int *what); /* the team of a CRugbyTeam (what 0), a human control (1), a player (2); -1 */
uint32_t ms_ball_va(void); /* BallManager* or 0 */

/* match_camera.c: the match view (16 floats: eye, target, up, fov, aspect, near, far) */
#define MS_CAMERA_BYTES  64
#define MS_HEAD_BYTES    (32 + MS_CAMERA_BYTES + MS_CONTROLS_BYTES + 4)   /* match_replay.c record(): + u8 directed */
#define MS_HEAD_DIRECTED (32 + MS_CAMERA_BYTES + MS_CONTROLS_BYTES)
void ms_camera_after(uint32_t ctrl, uint32_t cam, uint32_t proj);   /* game/overrides/match/camera_update.c */
void ms_camera_last(uint8_t *out);        /* this game's view at the last Present */
void ms_camera_game(uint8_t *out);        /* the game camera's own computed view (its controller's 16 floats) */
void ms_camera_event(uint32_t listener, uint32_t event);  /* the CameraManager hears a match event */
void ms_camera_event_done(uint32_t event);                 /* ... and has picked its camera (RT_MS_CAMEV) */
void ms_camera_host(const uint8_t *cam);                  /* a friend shows this view (NULL: its own) */
void ms_camera_remote(uint32_t id, uint32_t param);       /* a friend: the host's camera event ('C') */
int ms_camera_directed(void);                             /* the game shows a set piece's or event's camera */
void ms_camera_manager(uint32_t manager);                 /* the CameraManager (its listener inside) */
void own_end_heard(uint32_t event);   /* own_end.c: the camera heard a match event (every game) */
void own_end_present(void);           /* ... every Present: this player's end, drawn only */
const char *ms_camera_name(void);                         /* the camera this game shows ("Side", "Scrum" ...) */

/* match_measure.c: RT_MS_POS lines for tools/net/match_compare.py */
float ms_time(void); /* match time (CRugby+0x58), the replay frames' time base */
void ms_measure_update_begin(void); /* around CRugby::Update: its host CPU time */
void ms_measure_update_end(void);
void ms_measure_frame(uint32_t frame); /* ms_frame calls it */

/* net_ha.c: the match over the network (host-authoritative) */
void net_ha_setup(const uint8_t *setup);    /* host, at the launch */
void net_ha_frame_out(uint32_t frame_no, const uint8_t *head, uint32_t frame, uint32_t rec, uint32_t rec_len);
const uint8_t *net_ha_newest(int *fresh);   /* friend: newest host head (MS_HEAD_BYTES) + frame, or NULL */
void net_ha_send(char type, uint32_t frame, const void *payload, size_t len);
void net_ha_send_end(uint32_t frame);
void net_ha_nudge(int dx, int frames);   /* friend: push its sent stick left/right (-1/1) for some frames */
int net_ha_host_screen(void);
/* friend: the host's front-end screen id ('M'; 0 not in the menus, -1 unknown) */ /* host: E with its last frame's sequence number */
int net_ha_rx_recent(uint32_t frame, uint32_t within); /* a host frame arrived in the last `within` Presents */
/* net_ha_flow.c: friends follow the host into and out of its matches */
void net_ha_flow_setup(const uint8_t *setup);
void net_ha_flow_end(void);
void net_ha_flow_pause_request(void);
void net_ha_flow_resume_request(void); /* host: a friend's RESUME ('Y') */
int net_ha_flow_paused(void); /* this game's match is paused (its pause menu is open) */
void net_ha_flow_host_paused(int paused);
void net_ha_flow_present(uint32_t frame);

void ms_frame(uint32_t frame); /* Present hook: RT_MS_LOG=N trace line every N frames */

#ifdef __cplusplus
}
#endif
#endif
