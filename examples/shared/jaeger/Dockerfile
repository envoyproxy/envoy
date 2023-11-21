FROM jaegertracing/all-in-one@sha256:f2354c0c3ca701fa427e65f8d4f76f105264f14bc8f85854ec0afb46d31ec1f4
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
