FROM jaegertracing/all-in-one@sha256:11647571792ad9be00bc1c6ae58369063f38ea7ed6dc3e5edddd09513f919fa9
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
