FROM jaegertracing/all-in-one@sha256:1e0605eceeb82be9c1bf5c69f7139f6443fca76efcf8cb7b1b310e75676e59dc
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
