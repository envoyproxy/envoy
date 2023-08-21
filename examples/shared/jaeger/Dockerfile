FROM jaegertracing/all-in-one@sha256:246ba79a10184cfa1a34b4ec7a67aaa5b3e3734bb7fac85e6b46b5f6f0efd902
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
