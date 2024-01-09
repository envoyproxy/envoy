FROM jaegertracing/all-in-one@sha256:60e65bfffe1f66aee96d6fc8b6bf65afd42016be5dcc09d2e15d8462f7d9409c
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
