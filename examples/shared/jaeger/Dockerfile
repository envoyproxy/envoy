FROM jaegertracing/all-in-one@sha256:9af665a5f90103d610332d326c73e6b7d1c4d967774b6464b8fef716a8843f26
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
