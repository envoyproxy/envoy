FROM jaegertracing/all-in-one@sha256:6d97ff9034056002eccae6f9efcfdd9fd732be7149c1ffba0cc3d952828645b3
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
