FROM jaegertracing/all-in-one@sha256:e689838837df0f3841a18a8aa5ae090550f0bcf0a47a16e349772e614546f019
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
