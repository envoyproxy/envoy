FROM jaegertracing/all-in-one@sha256:52ff627ba3e38b41404026207f135e6a411aa8c643f0b201c1cb4626017e383f
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
