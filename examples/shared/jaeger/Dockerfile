FROM jaegertracing/all-in-one@sha256:e70f29aed92fe182dac4fc52f5c2bed35dc4645a465f0a9f0b232dc2e093aca9
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
