FROM jaegertracing/all-in-one@sha256:aa450ca8ac0afd03baf23cdf973dafc0d766c2f82e61749e00399442daf5f58c
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
