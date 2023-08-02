FROM jaegertracing/all-in-one@sha256:2e210e436537ed77e797ce4e039621d386c0c3c8fabbc975edab20ad8c0b4bb9
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
