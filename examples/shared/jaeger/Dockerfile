FROM jaegertracing/all-in-one@sha256:308b14daf2ddbc171bd5ce882dd5b12c1bac6446913fbb60b69424f4a34b7ade
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
