FROM jaegertracing/all-in-one@sha256:67848abb72bbd41f907d3a938b467020d29afe991ce38673b586251d557d9964
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
