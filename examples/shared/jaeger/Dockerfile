FROM jaegertracing/all-in-one@sha256:0c6b984e2433edb22935545d24391ab51ed05a6a38d80a7a24bd994540520749
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
