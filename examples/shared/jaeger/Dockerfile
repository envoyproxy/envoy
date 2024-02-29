FROM jaegertracing/all-in-one@sha256:97c69de9758ad682c69c31b81e0cdb6b954394f88798c17ddaa2f2b25d64f6a2
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
