FROM jaegertracing/all-in-one@sha256:c2901f612bea748fd5a8cc75f7f4c9d89bd569b0e8f333ac84181482e2fefa35
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
