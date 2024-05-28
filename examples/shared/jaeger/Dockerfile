FROM jaegertracing/all-in-one@sha256:353f8d2229cf51f0f76d85c506676a8ecc1eb0200d84deb3ba78ed34794be6b7
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
