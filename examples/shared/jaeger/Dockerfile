FROM jaegertracing/all-in-one@sha256:bd3826de29b0d9210549429bf8966776f866edecbc036144211ebd85e7cfe455
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
