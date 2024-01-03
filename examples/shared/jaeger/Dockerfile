FROM jaegertracing/all-in-one@sha256:c7f7e9460de8eb06ab43732b41f6c9308f0a11c563459bd3717753232a3fafb3
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
