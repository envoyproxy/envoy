.. _arch_overview_global_rate_limit:

Global rate limiting
====================

Although distributed :ref:`circuit breaking <arch_overview_circuit_break>` is generally extremely
effective in controlling throughput in distributed systems, there are times when it is not very
effective and global rate limiting is desired. The most common case is when a large number of hosts
are forwarding to a small number of hosts and the average request latency is low (e.g.,
connections/requests to a database server). If the target hosts become backed up, the downstream
hosts will overwhelm the upstream cluster. In this scenario it is extremely difficult to configure a
tight enough circuit breaking limit on each downstream host such that the system will operate
normally during typical request patterns but still prevent cascading failure when the system starts
to fail. Global rate limiting is a good solution for this case.

Envoy integrates directly with a global gRPC rate limiting service. Although any service that
implements the defined RPC/IDL protocol can be used, Envoy provides a `reference implementation <https://github.com/envoyproxy/ratelimit>`_
written in Go which uses a Redis backend. Envoy’s rate limit integration has the following features:

* **Network level rate limit filter**: Envoy will call the rate limit service for every new
  connection on the listener where the filter is installed. The configuration specifies a specific
  domain and descriptor set to rate limit on. This has the ultimate effect of rate limiting the
  connections per second that transit the listener. :ref:`Configuration reference
  <config_network_filters_rate_limit>`.
* **HTTP level rate limit filter**: Envoy will call the rate limit service for every new request on
  the listener where the filter is installed and where the route table specifies that the global
  rate limit service should be called. All requests to the target upstream cluster as well as all
  requests from the originating cluster to the target cluster can be rate limited.
  :ref:`Configuration reference <config_http_filters_rate_limit>`

Rate limit service :ref:`configuration <config_rate_limit_service>`.

Note that Envoy also supports :ref:`local rate limiting <config_network_filters_local_rate_limit>`.
Local rate limiting can be used in conjunction with global rate limiting to reduce load on the
global rate limit service. For example, a local token bucket rate limit can absorb very large bursts
in load that might otherwise overwhelm a global rate limit service. Thus, the rate limit is applied
in two stages. The initial coarse grained limiting is performed by the token bucket limit before
a fine grained global limit finishes the job.
