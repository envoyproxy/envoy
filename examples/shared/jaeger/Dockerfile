FROM jaegertracing/all-in-one@sha256:f251c87e11462715ecc4f752404b0cee7733d03ca41326e08622a7ece66c9f90
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
