#!/bin/sh
# ls.mixes: every mix of up to 4 players in a party (tools/net/ls_mixes_check.py; MIX=H,A,... for one).
cd "$(dirname "$0")/../.." || exit 1
exec /opt/homebrew/bin/python3.14 tools/net/ls_mixes_check.py
