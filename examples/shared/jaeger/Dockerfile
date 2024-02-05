FROM jaegertracing/all-in-one@sha256:6b345b7bdf2d47499d7f666b2a2260e659f203d2ab6b8b9215be104306cf19b4
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
