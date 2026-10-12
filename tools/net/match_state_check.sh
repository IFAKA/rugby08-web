#!/bin/sh
# on.match_map: the match read from guest memory (runtime/core/match_state.c) is consistent with the
# game. Native deterministic run of tests/replays/match_play.txt with RT_MS_LOG=60; the "ms:" lines must
# show 30 players on the pitch (or parked for a cutscene), the clock only moving forward and running
# in open play, the ball within 2 m of its holder, the phases kickoff -> open play -> a set piece, and
# the score the HUD shows (0-3 from frame 17500 on, screenshots in docs/MATCH_STATE.md). One summary line.
#   tools/net/match_state_check.sh [--frames N] [--timeout S]
# Run output: logs/match_state_check.log.
cd "$(dirname "$0")/../.." || exit 1
FRAMES=30000 TMO=2400
while [ $# -gt 0 ]; do
  case "$1" in
    --frames)
      FRAMES=$2
      shift 2
      ;;
    --timeout)
      TMO=$2
      shift 2
      ;;
    *)
      echo "match_state_check: unknown option $1"
      exit 2
      ;;
  esac
done
mkdir -p logs
OUT=logs/match_state_check.log
grep -v ' shot ' tests/replays/match_play.txt >logs/match_state_replay.txt
RT_DET=1 RT_FPS=0 RT_MS_LOG=60 SDL_AUDIODRIVER=dummy timeout -s KILL "$TMO" build/native/rugby08 --frames "$FRAMES" \
  --replay logs/match_state_replay.txt --log logs/match_state_run.log >"$OUT" 2>&1
.venv/bin/python -I tools/net/match_state_eval.py "$OUT"
