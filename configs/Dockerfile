# This configuration will build a Docker container containing
# an Envoy proxy that routes to envoyproxy.io.

FROM envoyproxy/envoy-dev:latest
RUN apt-get update
COPY envoyproxy_io_proxy.yaml /etc/envoy.yaml
CMD /usr/local/bin/envoy -c /etc/envoy.yaml
