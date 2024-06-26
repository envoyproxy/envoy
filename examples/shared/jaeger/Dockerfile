FROM jaegertracing/all-in-one@sha256:91f27082dfd75e18df0023b093954d004d59c37b184ecf036f3183f26048e6e1
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
