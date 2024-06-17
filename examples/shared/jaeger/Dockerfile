FROM jaegertracing/all-in-one@sha256:a61c2db90725164149a0bcccd81fcf9faa3f221d5afe6245ccf6a27654826e30
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
