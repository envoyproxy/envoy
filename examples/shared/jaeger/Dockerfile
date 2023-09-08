FROM jaegertracing/all-in-one@sha256:0edcea978eb4a631a0d9078f435908852f20013cf5d66908521c5ac2815aeb28
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
