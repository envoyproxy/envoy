.. _arch_overview_http_routing:

HTTP routing
============

Envoy includes an HTTP :ref:`router filter <config_http_filters_router>` which can be installed to
perform advanced routing tasks. This is useful both for handling edge traffic (traditional reverse
proxy request handling) as well as for building a service to service Envoy mesh (typically via
routing on the host/authority HTTP header to reach a particular upstream service cluster). At a high
level the router takes an incoming HTTP request, matches it to an upstream cluster, acquires a
:ref:`connection pool <arch_overview_conn_pool>` to a host in the upstream cluster, and forwards the
request. The router filter supports the following features:

* Virtual hosts that map domains/authorities to a set of routing rules.
* Prefix and exact path matching rules (both case sensitive and case insensitive). Regex/slug
  matching is not currently supported, mainly because it makes it difficult/impossible to
  programmatically determine whether routing rules conflict with each other. For this reason we
  don’t recommend regex/slug routing at the reverse proxy level, however we may add support in the
  future depending on demand.
* TLS redirection at the virtual host level.
* Path/host redirection at the route level.
* Host rewriting.
* Prefix rewriting.
* Request retries specified either via HTTP header or via route configuration.
* Request timeout specified either via HTTP header or via route configuration.
* Runtime configuration routing rules.
* Content type routing rules.
* Virtual cluster specifications. A virtual cluster is specified at the virtual host level and is
  used by Envoy to generate additional statistics on top of the standard cluster level ones. Virtual
  clusters can use regex matching.

Route table
-----------

The :ref:`configuration <config_http_conn_man>` for the HTTP connection manager owns the :ref:`route
table <config_http_conn_man_route_table>` that is used by all configured HTTP filters. Although the
router filter is the primary consumer of the route table, other filters also have access in case
they want to make decisions based on the ultimate destination of the request. For example, the built
in rate limit filter consults the route table to determine whether the global rate limit service
should be called based on the route. The connection manager makes sure that all calls to acquire a
route are stable for a particular request, even if the decision involves randomness (e.g. in the
case of a runtime configuration route rule).

.. _arch_overview_http_routing_retry:

Retry semantics
---------------

Envoy allows retries to be configured both in the :ref:`route configuration
<config_http_conn_man_route_table_route_retry>` as well as for specific requests via :ref:`request
headers <config_http_filters_router_headers>`. The following configurations are possible:

* **Maximum number of retries**: Envoy will continue to retry any number of times. An exponential
  backoff algorithm is used between each retry. Additionally, *all retries are contained within the
  overall request timeout*. This avoids long request times due to a large number of retries.
* **Retry conditions**: Envoy can retry on different types of conditions depending on application
  requirements. For example, network failure, all 5xx response codes, idempotent 4xx response codes,
  etc.
