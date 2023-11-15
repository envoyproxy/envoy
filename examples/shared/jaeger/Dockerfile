FROM jaegertracing/all-in-one@sha256:2425913e88cb90719b2954c60c716e003cf3a02b6ceb2d63c0d81f0896e88870
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
