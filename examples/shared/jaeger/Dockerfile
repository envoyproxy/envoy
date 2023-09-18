FROM jaegertracing/all-in-one@sha256:55cca56aeecda4cb5cbcf204b72d2ff34ff38cce5f35238c761f48712b1d3171
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
