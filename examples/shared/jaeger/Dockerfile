FROM jaegertracing/all-in-one@sha256:3f55be48264a6ed0e5b23caf31a08d8eea558dff1c676de4d6b02d4121a3a66c
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
