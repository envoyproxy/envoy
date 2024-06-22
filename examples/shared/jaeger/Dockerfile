FROM jaegertracing/all-in-one@sha256:acaab0b2e6dcfb37ddb65c7f716f31cd397df5131593d9bc40f8855454eda90f
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
