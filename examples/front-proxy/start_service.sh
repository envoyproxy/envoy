#!/bin/bash
python /code/service.py &
envoy -c /etc/service-envoy.json --service-cluster service${SERVICE_NAME}
