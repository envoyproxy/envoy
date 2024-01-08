#!/bin/bash

yarn
yarn build

BASE=$(yq -c < ../xds/lds.tmpl.yml)
ASSETS=$(find dist -type f)
echo "$ASSETS" \
    | jq  --arg base "$BASE" -Rs -f /usr/local/share/routes.jq \
 > ../xds/lds.update.yml
mv -f ../xds/lds.update.yml ../xds/lds.yml
