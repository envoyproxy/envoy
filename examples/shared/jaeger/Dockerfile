FROM jaegertracing/all-in-one@sha256:2960e2b293eb9d312b1588aa41395e5310b9e513319da238e1217049be060bac
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
