FROM jaegertracing/all-in-one@sha256:b50815b6c8c74ea3fcdf5deb93f8e5584af19808506f2ab829c066306e61596c
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
