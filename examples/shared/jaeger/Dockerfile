FROM jaegertracing/all-in-one@sha256:141f3dd2960689198da31635dcaf1ccc7dd443bb018bb6c3c1d2ab5d1449128d
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
