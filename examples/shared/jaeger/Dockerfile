FROM jaegertracing/all-in-one@sha256:40668208aacabb67d2e64d632bc1a15b7f6b54ca7b4f6a447ff938f4cdab1926
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
