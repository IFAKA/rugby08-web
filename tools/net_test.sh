#!/bin/sh
# Netplay end-to-end: relay server + two native instances in one room, lockstep from boot.
# Player 1 drives its virtual pad from a replay, player 2 from another; both must reach the last frame
# with identical state hashes (netplay exchanges them every 120 frames and stops on a mismatch).
#   tools/net_test.sh [--frames N] [--p1 FILE] [--p2 FILE] [--delay D] [--timeout S]
# Logs: logs/net_server.log, logs/net_p{1,2}.log (game), logs/net_out{1,2}.log (trace).
cd "$(dirname "$0")/.." || exit 1
FRAMES=3000 P1=tests/replays/net_p1.txt P2=tests/replays/net_p2.txt DELAY=4 TMO=900 PORT=${PORT:-8091}
while [ $# -gt 0 ]; do
  case "$1" in
    --frames) FRAMES=$2; shift 2;; --p1) P1=$2; shift 2;; --p2) P2=$2; shift 2;;
    --delay) DELAY=$2; shift 2;; --timeout) TMO=$2; shift 2;;
    *) echo "net_test: unknown option $1"; exit 2;;
  esac
done
mkdir -p logs/net
(cd server && go build -o ../build/rugby08-server .) || { echo "net_test: FAIL (server build)"; exit 1; }
build/rugby08-server -addr 127.0.0.1:$PORT -dir build/web >logs/net_server.log 2>&1 & SRV=$!
sleep 0.5
ROOM=T$$
run() {   # $1 player number, $2 replay
  RT_FPS=${RT_FPS:-0} SDL_AUDIODRIVER=dummy timeout -s KILL "$TMO" build/native/rugby08 --frames "$FRAMES" --replay "$2" \
    --log "logs/net_p$1.log" --net ws://127.0.0.1:$PORT/ws --net-room "$ROOM" --net-delay "$DELAY" \
    --net-name "P$1" >"logs/net_out$1.log" 2>&1
}
RT_FPS=${FPS1:-0} run 1 "$P1" & A=$!   # FPS1=60: player 1 plays in real time (a late joiner catches up)
sleep ${JOIN_AFTER:-0.3}   # JOIN_AFTER=S: player 2 drops in S seconds into player 1's game
RT_NET_FAULT=${FAULT:-} RT_POV=${POV2:-0} run 2 "$P2" & B=$!   # POV2: player 2 watches through its own camera
wait $A; RA=$?; wait $B; RB=$?
kill $SRV 2>/dev/null; wait $SRV 2>/dev/null
grep '^netplay: frame' logs/net_out1.log > logs/net_hash1.txt; grep '^netplay: frame' logs/net_out2.log > logs/net_hash2.txt
n1=$(wc -l < logs/net_hash1.txt | tr -d ' ') n2=$(wc -l < logs/net_hash2.txt | tr -d ' ')
desync=$(grep -h 'DESYNC' logs/net_out1.log logs/net_out2.log | head -1)
if [ -z "$desync" ] && [ "$n1" -ge $((FRAMES / 120 - 1)) ] && cmp -s logs/net_hash1.txt logs/net_hash2.txt; then
  echo "net_test: ok ($n1 hashes equal over $FRAMES frames, exits $RA/$RB)"; exit 0
fi
echo "net_test: FAIL (${desync:-hashes $n1/$n2 or differ}; exits $RA/$RB; last: $(tail -1 logs/net_out2.log | cut -c1-90))"; exit 1
