FROM jaegertracing/all-in-one@sha256:1f2bcc204e94964a1cecf7d419f5f374221292a7f4cbef840b7e9d7aee36eea6
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
