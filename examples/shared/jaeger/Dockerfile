FROM jaegertracing/all-in-one@sha256:eafd094d618cf0aa3d31d7b6576962cafcb5837f54ad785fc030e0955765fd35
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
