FROM jaegertracing/all-in-one@sha256:89ac615874459063a53eb6690e92a22c5cb904aebf779e17e589386c853f6603
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
