FROM jaegertracing/all-in-one@sha256:6c30b84069c9d652791e42840769934e9b129631ddc70b86f8ae7e5072ef1428
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
