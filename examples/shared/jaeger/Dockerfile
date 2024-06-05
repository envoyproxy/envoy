FROM jaegertracing/all-in-one@sha256:fc291b051201a2631bb0069f9e0d5b4239c10d531681b6a24e92898f5996c3e1
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
