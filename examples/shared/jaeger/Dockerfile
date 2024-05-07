FROM jaegertracing/all-in-one@sha256:4a2e123b62d68c0d0ab7d024e3131d2d3bc66090cd0a602c3a9e548530b3231c
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
