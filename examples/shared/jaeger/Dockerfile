FROM jaegertracing/all-in-one@sha256:c0de27d227a7e149e06e2ad5c51a24b293041884fd178c9b791d4f474281a0fd
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
