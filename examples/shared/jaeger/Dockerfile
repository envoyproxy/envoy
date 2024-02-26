FROM jaegertracing/all-in-one@sha256:4452919fb13a7112d26cf3c6a2d1e85a01804815c38ea3e1e4e7a2fae59b9e3d
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
