# HBONE CONNECT wait

HBONE CONNECT wait holds transport readiness and application I/O until CONNECT
acceptance, allowing connection-failure retries before application bytes are sent.
It uses an internal upstream transport, a terminal TCP proxy filter, and an
application connection pool adapter. Configure all three together.

The implementation uses Envoy's existing internal listener, user-space socket pair,
TCP proxy and HTTP/2 connection pool. No core Envoy changes are required. Each CONNECT
is a separate stream; the existing pool continues multiplexing those streams over
outer HTTP/2 connections.

For double HBONE, the inner HTTP/2 cluster's shim transport wraps its TLS socket and
connects to an outer shim listener. Inner TLS starts after the outer CONNECT succeeds.
Each hop keeps separate readiness state; accepted streams retain normal TLS and
forwarding behavior.

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
