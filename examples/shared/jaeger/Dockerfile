FROM jaegertracing/all-in-one@sha256:c502845936163f2db8b6b96ecf4559ca206d6444dd510559a6e88b8268acd1ed
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
