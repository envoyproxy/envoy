FROM jaegertracing/all-in-one@sha256:80cfd59acd07f1db07a4016ef38630538e52e09e37ab1a5f1cf20f9f1236afe6
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
