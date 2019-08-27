#!/usr/bin/env bash

srv &
/usr/local/bin/envoy -c /etc/s2s-grpc-envoy.yaml
