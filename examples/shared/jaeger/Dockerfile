FROM jaegertracing/all-in-one@sha256:73bdebb906ea17c3a87bf3fbaab1c338d3df22bcae9a263348671602d605de84
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
