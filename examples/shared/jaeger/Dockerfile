FROM jaegertracing/all-in-one@sha256:040857c9c22ac25775b0213e4127b3a94521f8232c2ef3597402b812ab1ef3dd
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
