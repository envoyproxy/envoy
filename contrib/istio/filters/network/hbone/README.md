# HBONE origination shim

This proof of concept pairs an internal upstream transport with a TCP proxy filter
that reports CONNECT acceptance. It holds transport readiness and application I/O
until acceptance, preserving the router's connection-failure retry behavior.

The implementation uses Envoy's existing internal listener, user-space socket pair,
TCP proxy and HTTP/2 connection pool. No core Envoy changes are required. Each CONNECT
is a separate stream; the existing pool continues multiplexing those streams over
outer HTTP/2 connections.

Application clusters also use `envoy.upstreams.istio_hbone` to terminate permanent
CONNECT rejections before retry and outlier accounting. CONNECT 4xx returns a local
HTTP response with the same status, or closes raw TCP. CONNECT 5xx retains transient
connection-failure behavior. Application HTTP responses keep their normal policies.
The paired Istiod capability is `ENABLE_HBONE_REJECTION_HANDLING: "true"`, together
with the original shim opt-in. New images and patched Istiod are both required.

For double HBONE, the inner HTTP/2 cluster's shim transport wraps its TLS socket and
connects to an outer shim listener. This preserves outer rejection status and starts
inner TLS only after the outer CONNECT succeeds. Each hop keeps separate readiness
state; accepted streams retain normal TLS and forwarding behavior.

See [configuration and retry semantics](../../../../../docs/root/configuration/other_features/istio_hbone.rst).

Build and run the focused tests inside the repository's build container:

```sh
bazel test -c fastbuild --copt=-g0 --jobs=6 \
  //contrib/istio/filters/network/hbone/test:config_test \
  //contrib/istio/filters/network/hbone/test:hbone_test \
  //contrib/istio/filters/network/hbone/test:hbone_integration_test
```

The integration tests use an HTTP/2 fake HBONE peer to isolate CONNECT acceptance,
refusal, multiplexing and GOAWAY behavior. They do not exercise ztunnel or mutual TLS
identity configuration.

When retaining Istio's baggage peer-metadata filters, the listener and service-cluster
filters must remain paired. The upstream peer-metadata filter recognizes
`connect_originate_hbone_shim` and `inner_connect_originate_hbone_shim`, so it consumes
the metadata preamble before the application codec sees the response. Run
`//contrib/istio/filters/network/peer_metadata/test:peer_metadata_test` as well when
changing this integration.
