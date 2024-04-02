FROM jaegertracing/all-in-one@sha256:c528c25d39937af61dce9659793a9815251c79c74de52058dfcbbe2929e01128
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
