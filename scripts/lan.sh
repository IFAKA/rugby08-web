#!/usr/bin/env bash
# Party site on your LAN for a two-computer test: scripts/lan.sh
# Prints the link for the other computer. HTTPS is needed (the game's threads); mkcert makes the certificate.
set -euo pipefail
cd "$(dirname "$0")/.."
IP=$(ipconfig getifaddr en0 || ipconfig getifaddr en1)
mkdir -p build/lan
mkcert -install >/dev/null 2>&1
mkcert -cert-file build/lan/cert.pem -key-file build/lan/key.pem "$IP" localhost >/dev/null 2>&1
echo "Open on BOTH computers: https://$IP:8443"
echo "Other computer, once: trust $(mkcert -CAROOT)/rootCA.pem (double-click, Always Trust)"
cd server && exec go run . -addr :8443 -dir ../build/web -cert ../build/lan/cert.pem -key ../build/lan/key.pem
