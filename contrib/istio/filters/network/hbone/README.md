# HBONE origination shim

This proof of concept pairs an internal upstream transport with a TCP proxy filter
that reports CONNECT acceptance. It holds transport readiness and application I/O
until acceptance, preserving the router's connection-failure retry behavior.

The implementation uses Envoy's existing internal listener, user-space socket pair,
TCP proxy and HTTP/2 connection pool. No core Envoy changes are required. Each CONNECT
is a separate stream; the existing pool continues multiplexing those streams over
outer HTTP/2 connections.

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
