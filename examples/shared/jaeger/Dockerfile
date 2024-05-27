FROM jaegertracing/all-in-one@sha256:048c07e5a62443c90fa13162a5f2eae14919a2e3d541b1c500519b52643befbb
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
