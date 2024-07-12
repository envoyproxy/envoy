FROM jaegertracing/all-in-one@sha256:150471947075451e6d3631d852baa1d72b5123b5d239024ce42f20d4c5bd9dc8
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
