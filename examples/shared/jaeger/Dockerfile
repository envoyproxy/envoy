FROM jaegertracing/all-in-one@sha256:f32e8fbf805f92b8441a59d5bf8d7b3315d4e68a1587c6de38eb40a6d81c983c
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
