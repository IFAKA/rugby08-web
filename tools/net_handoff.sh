#!/bin/sh
# Netplay hand-off: player 1 creates the room, player 2 drops in, player 1 leaves (its game ends at
# frame 900), then player 3 drops in and must be admitted by player 2 (the server hands the role on).
# Pass: player 3 joins and catches up, and players 2 and 3 agree on every state hash they share.
# One summary line; logs/net_handoff_{server,out1,out2,out3}.log.
cd "$(dirname "$0")/.." || exit 1
PORT=8096 ROOM=H$$ TMO=400
(cd server && go build -o ../build/rugby08-server .) || { echo "net_handoff: FAIL (server build)"; exit 1; }
build/rugby08-server -addr 127.0.0.1:$PORT -dir build/web >logs/net_handoff_server.log 2>&1 & SRV=$!
sleep 0.5
run() {   # $1 player, $2 frames
  RT_FPS=60 SDL_AUDIODRIVER=dummy timeout -s KILL "$TMO" build/native/rugby08 --frames "$2" --log "logs/net_handoff_p$1.log" \
    --net ws://127.0.0.1:$PORT/ws --net-room "$ROOM" --net-name "P$1" >"logs/net_handoff_out$1.log" 2>&1
}
run 1 900 & A=$!
sleep 4; run 2 2400 & B=$!
sleep 22; run 3 2400 & C=$!
wait $A; wait $B; RB=$?; wait $C; RC=$?
kill $SRV 2>/dev/null; wait $SRV 2>/dev/null
h() { grep '^netplay: frame' "logs/net_handoff_out$1.log"; }
h 2 > logs/net_handoff_h2.txt; h 3 > logs/net_handoff_h3.txt
joined=$(grep -o 'caught up at frame [0-9]*' logs/net_handoff_out3.log | head -1)   # (it takes the freed controller 1)
n=$(wc -l < logs/net_handoff_h3.txt | tr -d ' ')
if [ -n "$joined" ] && grep -q 'now admits newcomers' logs/net_handoff_out2.log \
   && [ "$n" -ge 15 ] && ! grep -q DESYNC logs/net_handoff_out2.log logs/net_handoff_out3.log && cmp -s logs/net_handoff_h2.txt logs/net_handoff_h3.txt; then
  echo "net_handoff: ok (newcomer $joined after player 1 left; $n hashes equal, exits $RB/$RC)"; exit 0
fi
echo "net_handoff: FAIL (${joined:-player 3 never joined}; hashes $n; exits $RB/$RC; last: $(tail -1 logs/net_handoff_out3.log | cut -c1-90))"; exit 1
