FROM jaegertracing/all-in-one@sha256:53ce134bd245fe59802bdc2cc21d4ab606c662fc5a371cb1e825f9bd1312a068
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
