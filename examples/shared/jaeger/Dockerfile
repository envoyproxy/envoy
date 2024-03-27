FROM jaegertracing/all-in-one@sha256:4a334785966c4c40823f1e4abea9fa2bc4fe12605b2a07cc2c00e632ce218b18
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
