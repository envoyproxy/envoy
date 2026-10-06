Removed the ``envoy.network.connection_balance.dlb`` contrib extension (Intel DLB connection
balancer), along with the ``dlb`` Bazel dependency, because the upstream Intel source archive is no
longer available and there is no evidence of any users. See
https://github.com/envoyproxy/envoy/issues/45491 for background.
