FROM jaegertracing/all-in-one@sha256:6b217790805d27ed76f6d883446749819b0a16cdbbf99b9cfea70f417861b8a0
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
