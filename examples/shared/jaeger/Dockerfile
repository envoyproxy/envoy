FROM jaegertracing/all-in-one@sha256:e2798b9007d7f5b6859279069eb99145b478d20fc3596955c9c43514b330058f
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
