FROM jaegertracing/all-in-one@sha256:76249a2662662213b307a03cad9502ef80e41c40cdffa35c8dac43f77be2201c
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
