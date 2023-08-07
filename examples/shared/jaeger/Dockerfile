FROM jaegertracing/all-in-one@sha256:d0920f88eeeb337bb1f30bbbfd8142ef0dfb60ba4d2bc3ded33d525234250e5c
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
