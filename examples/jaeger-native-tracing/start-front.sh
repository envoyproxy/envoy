#!/usr/bin/env bash
/install-jaeger-plugin.sh
/usr/local/bin/envoy -c /etc/front-envoy.yaml --service-cluster front-proxy
