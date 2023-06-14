FROM jaegertracing/all-in-one@sha256:c7972c5575634820aa21cbc57475ba87a5b823f7e0f5608f30dc219f4d38a702
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
