FROM jaegertracing/all-in-one@sha256:158ce9104d91f955e1515655ebba67b686e62c6cf09ce65eb0942ef14723c16b
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
