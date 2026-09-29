#!/usr/bin/env bash
# Unattended driver: runs fresh Claude Code sessions until features.json is all
# passing, or until 3 sessions in a row make no new passing item (then stops so
# a human can review the BLOCKED note at the top of PROGRESS.md).
# Usage: scripts/run.sh [max_sessions]   (env CLAUDE_FLAGS for extra flags,
#   e.g. CLAUDE_FLAGS="--permission-mode auto")
cd "$(dirname "$0")/.." || exit 1
mkdir -p logs/sessions
max=${1:-50}; stall=0
passing(){ jq '[.items[]|select(.passes)]|length' features.json; }
total=$(jq '.items|length' features.json)
PROMPT='Follow CLAUDE.md "Session protocol". Start with krn context, tail -40 PROGRESS.md, ./init.sh status. Work through runnable items in features.json until your context is getting full or everything is blocked; commit and log PROGRESS.md after each passing item.'
for i in $(seq 1 "$max"); do
  before=$(passing)
  [ "$before" -ge "$total" ] && { echo "run: all $total items pass"; exit 0; }
  echo "run: session $i starting ($before/$total passing, next: $(./init.sh next))"
  # shellcheck disable=SC2086
  claude -p "$PROMPT" --model claude-opus-5-5 --effort medium $CLAUDE_FLAGS \
    > "logs/sessions/$(date +%Y%m%dT%H%M%S).log" 2>&1
  after=$(passing)
  if [ "$after" -gt "$before" ]; then stall=0; else stall=$((stall+1)); fi
  echo "run: session $i done ($after/$total passing, stall=$stall)"
  if [ "$stall" -ge 3 ]; then
    echo "run: 3 sessions without progress; stopping. See top of PROGRESS.md"; exit 1
  fi
done
