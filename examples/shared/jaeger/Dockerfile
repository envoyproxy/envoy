FROM jaegertracing/all-in-one@sha256:08ab322954905dd2b9885082f702364e7b1d7146172590385bba5edefc95cc13
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
