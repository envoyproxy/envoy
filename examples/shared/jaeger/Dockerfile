FROM jaegertracing/all-in-one@sha256:4f85b75673c0863ef869f3e54d043f842935351ef0856d5ddad1709da4f17354
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
