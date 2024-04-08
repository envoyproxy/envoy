FROM jaegertracing/all-in-one@sha256:cc0737bacabf61887becfed93a2f68c8af1627e0e7842bd4a29304f2f04b332f
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
