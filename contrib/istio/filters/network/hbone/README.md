# HBONE CONNECT wait

HBONE CONNECT wait holds transport readiness and application I/O until CONNECT
acceptance, allowing connection-failure retries before application bytes are sent.
The HBONE CONNECT adapter uses an internal upstream transport, the standard TCP proxy
filter, and a connection pool adapter on both application and HBONE clusters.

The implementation uses Envoy's existing internal listener, user-space socket pair,
TCP proxy and HTTP/2 connection pool. No core Envoy changes are required. Each CONNECT
is a separate stream; the existing pool continues multiplexing those streams over
outer HTTP/2 connections.

For double HBONE, both HBONE clusters select the pool adapter. The inner HTTP/2 cluster's
adapter transport wraps its TLS socket and connects to the outer origination listener.
Inner TLS starts after the outer CONNECT succeeds.
Each hop keeps separate readiness state; accepted streams retain normal TLS and
forwarding behavior.

The network configuration factory remains available as a validating alias for stock
TCP proxy. CONNECT signaling lives in the pool adapter; there is no TCP proxy subclass.
Pilot must configure the pool adapter on the HBONE clusters before using this version.

See [configuration and retry semantics](../../../../../docs/root/configuration/other_features/istio_hbone.rst).

Build and run the focused tests inside the repository's build container:

```sh
bazel test -c fastbuild \
  //contrib/istio/filters/network/hbone/test:config_test \
  //contrib/istio/filters/network/hbone/test:hbone_test \
  //contrib/istio/filters/network/hbone/test:pool_test \
  //contrib/istio/filters/network/hbone/test:hbone_integration_test
```

The integration tests use an HTTP/2 fake HBONE peer to isolate CONNECT acceptance,
refusal, multiplexing and GOAWAY behavior. They do not exercise ztunnel or mutual TLS
identity configuration.

When retaining Istio's baggage peer-metadata filters, the listener and service-cluster
filters must remain paired. The upstream peer-metadata filter recognizes
`connect_originate_hbone_adapter` and `inner_connect_originate_hbone_adapter`, so it consumes
the metadata preamble before the application codec sees the response. Run
`//contrib/istio/filters/network/peer_metadata/test:peer_metadata_test` as well when
changing this integration.
