FROM jaegertracing/all-in-one@sha256:5734f22d8aacb7cd2998f9a62d409c1bf789dc33524d65766b8c897d658a1d04
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
