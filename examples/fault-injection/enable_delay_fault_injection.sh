#!/usr/bin/env bash
set -ex

mkdir -p /srv/runtime/v1/envoy/fault/http/delay
echo '50' > /srv/runtime/v1/envoy/fault/http/delay/fixed_delay_percent
echo '3000' > /srv/runtime/v1/envoy/fault/http/delay/fixed_duration_ms

pushd /srv/runtime
ln -s /srv/runtime/v1 new && mv -Tf new current
popd
