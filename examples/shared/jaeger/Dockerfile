FROM jaegertracing/all-in-one@sha256:cb7b4b246afae0e02cecc567bee9df7bea9ee3d9a710aec78f73865e58ffe930
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
