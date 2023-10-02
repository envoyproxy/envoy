FROM jaegertracing/all-in-one@sha256:d5fa42548a5839d1585ae05ea9fb3d839eb29978141a1e9f372d580a18164593
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
