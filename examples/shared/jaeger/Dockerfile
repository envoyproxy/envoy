FROM jaegertracing/all-in-one@sha256:e9351f6d0b1e75f6ec30daf433e2459a7d528a49ecbac2e00206a1cb0fb90958
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
