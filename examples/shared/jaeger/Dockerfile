FROM jaegertracing/all-in-one@sha256:aea7bc12665c0cc795a25b8db804000f0a7339a2b6edfedd477da80476b8c3a1
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
