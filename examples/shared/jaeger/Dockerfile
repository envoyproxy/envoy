FROM jaegertracing/all-in-one@sha256:310678df313936a95bcb19e1992fe557e48670da5ae9a2bbc9350d3bad0b1711
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
