FROM jaegertracing/all-in-one@sha256:a0fa2344a3ec51d0fbf7d05fc9662c45401d633230f04fa1f2ddd2e3fdea6ea5
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
