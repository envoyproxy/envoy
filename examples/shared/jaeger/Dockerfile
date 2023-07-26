FROM jaegertracing/all-in-one@sha256:93ba12f6986a566d12929b29a6117b7f7a9179e49654f9814e0c50e364fd93af
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
