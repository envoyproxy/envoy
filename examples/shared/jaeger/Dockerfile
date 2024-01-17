FROM jaegertracing/all-in-one@sha256:7789c70591ab7bc200e05cf86c5cfee86333b21e40ace4df0c9a094bd928cd1d
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
