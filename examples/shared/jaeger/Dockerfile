FROM jaegertracing/all-in-one@sha256:81f637437f8b82be0576458d970fc742e0efc9f0b0d351e2fec30dd2d7557ba4
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
