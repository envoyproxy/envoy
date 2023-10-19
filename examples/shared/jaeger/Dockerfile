FROM jaegertracing/all-in-one@sha256:4426158b2f6d7a2e30c18d754619ef4c47de8629db5e0bf6462e5bd0cbc0a8aa
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
