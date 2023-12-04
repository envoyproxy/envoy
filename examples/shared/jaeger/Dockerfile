FROM jaegertracing/all-in-one@sha256:6512be14fc019bca8aba2ff886be2dc37f99928e85b53e90d93e8b6a15bdeb00
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
