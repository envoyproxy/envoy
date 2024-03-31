FROM jaegertracing/all-in-one@sha256:15b4256cc70141664c34c7aaa7e261588bbbfba7951393a82a49ff3db9f0f402
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
