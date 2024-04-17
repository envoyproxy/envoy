FROM jaegertracing/all-in-one@sha256:c3d62f3a5ada9260d5067a194e3ae756910dc41fa83695e21a213624b1cd9f07
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
