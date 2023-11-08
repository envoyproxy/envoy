FROM jaegertracing/all-in-one@sha256:16a47d95fafa3446d23bf9a9fe2cba6b22a16d529718dd4ea98b1afc0c533b4c
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
