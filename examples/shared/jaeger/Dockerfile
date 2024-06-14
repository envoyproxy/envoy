FROM jaegertracing/all-in-one@sha256:0b0efdb42699e8b93a38cdf5649daefc271e39cb703ce5505c1f5e3a5404bc49
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
