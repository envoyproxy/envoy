FROM jaegertracing/all-in-one@sha256:5e5deff25ae75f309ff61da21b31ea93226ab3a1eefe8b54261c7616befdd2d3
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
