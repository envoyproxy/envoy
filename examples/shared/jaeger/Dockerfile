FROM jaegertracing/all-in-one@sha256:abd7a85e9b8a8dab6edacf311e63907ffdbe25326f1bce881dd06ac79199fee5
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
