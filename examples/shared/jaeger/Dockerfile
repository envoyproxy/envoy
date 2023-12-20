FROM jaegertracing/all-in-one@sha256:5bc790ff5c114adb961b64dbbdab9701d32b6c317c09cfb9597556a906a353b4
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
