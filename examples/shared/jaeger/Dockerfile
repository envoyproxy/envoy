FROM jaegertracing/all-in-one@sha256:fbd84e082ebb4fb2baae635b39affdd38203d6309ebafa921e05192cdaea3967
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
