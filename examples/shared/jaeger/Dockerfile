FROM jaegertracing/all-in-one@sha256:282e8eb9b384bab39b2cb65ee26b3896606da72ea64b5557ca14348250747cd4
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
