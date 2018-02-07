# This configuration will build a Docker container containing
# an Envoy proxy that routes to Google.

FROM envoyproxy/envoy:latest
RUN apt-get update
COPY google_com_proxy.v2.yaml /etc/envoy.yaml
CMD /usr/local/bin/envoy -c /etc/envoy.yaml
