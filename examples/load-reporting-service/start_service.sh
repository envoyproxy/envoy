#!/bin/sh
python3 /code/http_server.py &
/usr/local/bin/envoy -c /etc/service-envoy-w-lrs.yaml --service-node ${HOSTNAME} --service-cluster http_service
