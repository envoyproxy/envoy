FROM jaegertracing/all-in-one@sha256:ca036b85ceba3bc1364e9fb611ddaca1884c2950ff361616876009edc79900b0
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
