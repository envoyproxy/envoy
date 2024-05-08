FROM jaegertracing/all-in-one@sha256:bc6a57dfa300f73e7a28a781695b712dde74effea9291627cef53e2acf25cc32
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"
