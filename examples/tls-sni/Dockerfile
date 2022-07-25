FROM envoyproxy/envoy-dev:latest

COPY ./envoy.yaml /etc/envoy.yaml
COPY ./certs /certs
RUN chmod go+r /etc/envoy.yaml \
	&& chmod go+x /certs \
	&& chmod go+r /certs/*

CMD ["/usr/local/bin/envoy", "-c", "/etc/envoy.yaml"]
