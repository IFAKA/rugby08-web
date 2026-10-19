#!/bin/sh
# pt.deploy: the private party site's package (tools/net/deploy_package.sh) holds no game file, its server
# refuses pages without the party link, and through the link a player brings their own Rugby 08 and boots
# (tools/net/own_copy_check.py on the package). One line; details in logs/deploy_package.log, logs/own_copy/.
cd "$(dirname "$0")/../.." || exit 1
tools/net/deploy_package.sh | grep -q ': ok' || { echo "deploy_check: FAIL (package, logs/deploy_package.log)"; exit 1; }
(cd server && go test ./... >/dev/null) || { echo "deploy_check: FAIL (server tests)"; exit 1; }
OWN_SITE=build/party-site/site OWN_PASSWORD=deploy-check-party exec /opt/homebrew/bin/python3.14 tools/net/own_copy_check.py
