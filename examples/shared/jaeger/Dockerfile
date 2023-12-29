FROM jaegertracing/all-in-one@sha256:72b40da2c2b17dd6aaeff326f12204169e20a1e6193cc2cbaae39add79debd7d
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
