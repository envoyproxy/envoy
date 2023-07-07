FROM jaegertracing/all-in-one@sha256:92c3f8740a1babf2bb585774078ec1e2ca5ee5920f003a2a0988aab1f55789ca
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
