FROM jaegertracing/all-in-one@sha256:1354413cb587913eef2f9d6b55926bacbe167ffe25610936afab365d8327ac14
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
