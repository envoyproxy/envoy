# Developer-local docs build

See [data-plane-api](https://github.com/envoyproxy/data-plane-api/blob/master/docs/README.md).

# How the Envoy website and docs are updated

1. The docs are published to [docs/envoy/latest](https://github.com/envoyproxy/envoyproxy.github.io/tree/master/docs/envoy/latest)
   on every commit to master in [data-plane-api](https://github.com/envoyproxy/data-plane-api).
2. The docs are published to [docs/envoy](https://github.com/envoyproxy/envoyproxy.github.io/tree/master/docs/envoy)
   in a directory named after every tagged commit in this repo. Thus, on every tagged release there
   are snapped docs.
