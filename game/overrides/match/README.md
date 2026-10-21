# game/overrides/match/ — the match, in C (on.match_puppet)

A friend's game plays the host's match (docs/MATCH_STATE.md "The friend side of the puppet"):
- `rugby_update.c`: CRugby::Update (FUN_0062ED50): in a friend's game the match logic is skipped, the host
  frame is taken, the presentation steps run, then players and ball are placed where the host had them.
- `pose_player.c`: FUN_004039B0 poses each player: in a friend's game from the host's pose block, through
  the game's replay branch.
- `replay_playing.c`: FUN_00457690 ("a replay is playing") answers yes only while pose_player.c poses a
  player from the host's frame.

In every other game these are the original functions.
