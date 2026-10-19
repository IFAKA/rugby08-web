#!/bin/sh
# The private party site as one folder to copy to a server (docs/ONLINE.md "The party site"):
#   build/party-site/site/     the program (build/site resolved: real files) and the game files' manifest
#   build/party-site/rugby08-server-linux-{amd64,arm64}   the lobby/relay server
#   build/party-site/Caddyfile, run.sh, README.txt         HTTPS by Caddy (Let's Encrypt), the server behind it
# No game file goes in: the check refuses a package holding any chunk or any file of "Game Files".
# usage: tools/net/deploy_package.sh   (after ./init.sh build-web). One line; details in logs/deploy_package.log.
cd "$(dirname "$0")/../.." || exit 1
LOG=logs/deploy_package.log
OUT=build/party-site
mkdir -p logs
die() {
  echo "deploy_package: FAIL ($1, $LOG)"
  exit 1
}
{
  .venv/bin/python tools/web/site_manifest.py build/web build/site || exit 1
  rm -rf "$OUT" && mkdir -p "$OUT/site" || exit 1
  cp -RL build/site/. "$OUT/site/" || exit 1
  for a in amd64 arm64; do
    (cd server && CGO_ENABLED=0 GOOS=linux GOARCH=$a go build -o "../$OUT/rugby08-server-linux-$a" .) || exit 1
  done
  cat >"$OUT/Caddyfile" <<'EOF'
# HTTPS for the party site (Caddy gets the certificate). SITE_HOST: a domain pointing at this server, or
# the server's address as A-B-C-D.sslip.io (no domain needed).
{$SITE_HOST} {
	reverse_proxy 127.0.0.1:8090
}
EOF
  cat >"$OUT/run.sh" <<'EOF'
#!/bin/sh
# Start the party site: PARTY_PASSWORD=... SITE_HOST=... ./run.sh  (STUN on UDP 3478, HTTPS by Caddy)
cd "$(dirname "$0")" || exit 1
[ -n "$PARTY_PASSWORD" ] || { echo "set PARTY_PASSWORD (the party link is https://SITE_HOST/k/PASSWORD)"; exit 1; }
[ -n "$SITE_HOST" ] || { echo "set SITE_HOST (a domain, or A-B-C-D.sslip.io)"; exit 1; }
case "$(uname -m)" in aarch64 | arm64) arch=arm64 ;; *) arch=amd64 ;; esac
"./rugby08-server-linux-$arch" -addr 127.0.0.1:8090 -dir site -stun 3478 -password "$PARTY_PASSWORD" -logdir logs &
SITE_HOST="$SITE_HOST" exec caddy run --config Caddyfile
EOF
  chmod +x "$OUT/run.sh"
  cat >"$OUT/README.txt" <<'EOF'
Rugby 08 party site (private). On a Linux server with Caddy installed and ports 80, 443 (TCP) and
3478 (UDP) open:  PARTY_PASSWORD=a-long-password SITE_HOST=your.domain ./run.sh
Send friends https://your.domain/k/a-long-password . Each player picks their own Rugby 08 folder once.
EOF
} >"$LOG" 2>&1 || die "packaging failed"
# no game file in the package: no chunk, and no file equal to one of "Game Files"
find "$OUT" -name '*.bin' | grep -q . && die "a game file chunk is in the package"
find "Game Files/" -type f -exec shasum -a 256 {} + | cut -c1-64 | sort -u >logs/deploy_game.sha
find "$OUT" -type f -exec shasum -a 256 {} + | cut -c1-64 | sort -u >logs/deploy_pkg.sha
[ -z "$(comm -12 logs/deploy_game.sha logs/deploy_pkg.sha)" ] || die "a file of Game Files is in the package"
echo "deploy_package: ok ($(du -sh "$OUT" | cut -f1) in $OUT, no game file)"
