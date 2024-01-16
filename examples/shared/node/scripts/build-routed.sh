#!/bin/bash

yarn
yarn build

BASE=$(yq -c < /var/lib/envoy/lds.tmpl.yml)
ASSETS=$(find dist -type f)
echo "$ASSETS" \
    | jq  --arg base "$BASE" -Rs -f /usr/local/share/routes.jq \
 > /var/lib/envoy/lds.update.yml
mv -f /var/lib/envoy/lds.update.yml /var/lib/envoy/lds.yml
