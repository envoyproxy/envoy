FROM jaegertracing/all-in-one@sha256:1cb80937ebe12ed06267d2f773623d33a4610df9ef59d82e92c15613f31772ed
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
