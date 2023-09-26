FROM jaegertracing/all-in-one@sha256:f1c5c3613b093d2ef17827f7eac3a821611785be8b729ff9c215066777e47772
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
