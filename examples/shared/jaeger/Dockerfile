FROM jaegertracing/all-in-one@sha256:453b52f7bc627f4ea4b5f1b4ef39b76fc8e2a704e2e86fbf9b3c2b4c78fc7692
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
