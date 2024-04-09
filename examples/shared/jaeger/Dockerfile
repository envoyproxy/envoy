FROM jaegertracing/all-in-one@sha256:6a90f321a8107f69a28eed69775214c7cadba64138768be55876b4be79a82585
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
