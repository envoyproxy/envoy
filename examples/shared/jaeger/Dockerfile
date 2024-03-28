FROM jaegertracing/all-in-one@sha256:512034dbe674b89c2ca3298fb87ffe72cefdea19dbbb09a7453e30b3494820c7
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
