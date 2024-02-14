FROM jaegertracing/all-in-one@sha256:d96d4a09d32d11e7d7743e041d917cc6fd57677341c3d457cd45b2b0a0a8381f
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
