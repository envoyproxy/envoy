FROM jaegertracing/all-in-one@sha256:fca45d50fbde64153bdad81b6f11e011f13ac9758a87b2b6b01bf346dc59798c
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
