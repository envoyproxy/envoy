FROM jaegertracing/all-in-one@sha256:67f23644ab54e112253446ab2bc545910f574458c482cae447a33ad0e2732026
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
