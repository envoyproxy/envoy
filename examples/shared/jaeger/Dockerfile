FROM jaegertracing/all-in-one@sha256:6ea4f4ba30ac4e9c1b134d4ffd1ad55a12575b55c8dd1b306f93e78dfa925775
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
