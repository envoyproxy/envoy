#!/usr/bin/env bash
set -ex

echo '100' > /srv/runtime/v1/envoy/fault/http/abort/abort_percent
echo '503' > /srv/runtime/v1/envoy/fault/http/abort/http_status

pushd /srv/runtime
ln -s /srv/runtime/v1 new && mv -Tf new current
popd
