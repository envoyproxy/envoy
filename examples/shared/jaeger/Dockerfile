FROM jaegertracing/all-in-one@sha256:574fc7fac591cc1f22d810853e7d19fe12b69f29259e53baaf0ff54d2bc20725
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
