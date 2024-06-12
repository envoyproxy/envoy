FROM jaegertracing/all-in-one@sha256:29820d28bd3d29bba64b1a57b79bf15caf909c769c2b83ce9bdaf56bbb44e80a
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
