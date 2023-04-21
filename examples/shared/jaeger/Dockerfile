FROM jaegertracing/all-in-one@sha256:293d897dcbd484e4f5584c2b8f89058a1ec817ac454633990ab263d89203aff6
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
