FROM jaegertracing/all-in-one@sha256:cb0749c3f455fc19f490ada07b5de5c3611dca62280d86f321cdc4eb09b0ed57
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
