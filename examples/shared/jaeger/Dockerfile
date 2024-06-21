FROM jaegertracing/all-in-one@sha256:9a22cc340a9f518f792b9159e2eb09c1311dc2fc22894da26d1efdbbaabcea19
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
