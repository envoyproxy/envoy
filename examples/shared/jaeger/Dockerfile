FROM jaegertracing/all-in-one@sha256:1d6c9e68edf6cfb409acf21bdcf0900bc883ecb3521e834fc238b6eecb2a252d
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
