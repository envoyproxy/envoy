FROM jaegertracing/all-in-one@sha256:f11a7d9704eba38206be4f556b5590824c7f306d90456a24626a2c8239e22893
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
