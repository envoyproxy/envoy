FROM jaegertracing/all-in-one@sha256:056d25674a0ac1d5afeecd3c1a834d64916fc8397c1444aed1e27688154b4e53
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
