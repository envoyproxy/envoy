FROM jaegertracing/all-in-one@sha256:b9be3b7c52a97ae16293d41062471bae50c00401b29c5a1113dc6f3089a7a9aa
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
