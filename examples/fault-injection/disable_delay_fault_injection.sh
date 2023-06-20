#!/usr/bin/env bash
set -ex

rm /srv/runtime/v1/envoy/fault/http/delay/fixed_delay_percent
rm /srv/runtime/v1/envoy/fault/http/delay/fixed_duration_ms

pushd /srv/runtime
ln -s /srv/runtime/v1 new && mv -Tf new current
popd
