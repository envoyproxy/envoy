FROM jaegertracing/all-in-one@sha256:b49249c222993a19c94a55b08174dce1962652224cce5cb0a2b3a31115457703
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
