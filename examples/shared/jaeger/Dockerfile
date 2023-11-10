FROM jaegertracing/all-in-one@sha256:04a5f681b7c9405d68db8e5b0e2645493111c540391156b30eb281cc803f6d97
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
