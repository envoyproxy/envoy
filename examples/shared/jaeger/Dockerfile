FROM jaegertracing/all-in-one@sha256:12e96c7396d758da7d300d162f2aab899c2f6cc51e037cc7bced8dbd6d3cfc5d
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
