FROM jaegertracing/all-in-one@sha256:0e60fd3f7880f2d212ce29782b7a78a88892a7646c847cc38bc1a85aa0f22a04
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
