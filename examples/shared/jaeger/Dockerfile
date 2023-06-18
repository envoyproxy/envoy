FROM jaegertracing/all-in-one@sha256:fc197c49334b8f82cb4b4aba0557d3b0a4364f78867f3abaa19a1e16b4af6019
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
