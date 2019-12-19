#!/bin/sh

set -e
cat /tpl/envoy.yaml | envsubst "`printf '$%s' $(bash -c "compgen -e")`" > /etc/envoy.yaml
/usr/local/bin/envoy -c /etc/envoy.yaml --service-cluster ${SERVICE_NAME}
