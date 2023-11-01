FROM jaegertracing/all-in-one@sha256:d42e451d73188d1d47c60dff6cf645b73b7cc470a6463fcae03aa7dfb9ab8b59
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
