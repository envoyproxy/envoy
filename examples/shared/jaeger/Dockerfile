FROM jaegertracing/all-in-one@sha256:a13ec6bbe036293bb68b0e8f9648dc2bf266734d922ab08e87e03b4d5c1c9887
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
