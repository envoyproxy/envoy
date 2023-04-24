FROM jaegertracing/all-in-one@sha256:508634b55341cfcfd781e2c8ee7d30cabce74f34494df9517a22ca4b0dada673
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
