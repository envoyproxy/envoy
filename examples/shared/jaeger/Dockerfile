FROM jaegertracing/all-in-one@sha256:98d7dfe18ac0e0149e969d19548ee8ba23d14d7955e107f684549ceb28694dc0
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
