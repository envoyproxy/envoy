FROM jaegertracing/all-in-one@sha256:c9fe14cddc1039dd254bab32aa1194f82f79549130f5ea815cf6f2d9c0f30ef1
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
