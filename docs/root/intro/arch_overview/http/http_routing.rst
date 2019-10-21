.. _arch_overview_http_routing:

HTTP routing
============

Envoy includes an HTTP :ref:`router filter <config_http_filters_router>` which can be installed to
perform advanced routing tasks. This is useful both for handling edge traffic (traditional reverse
proxy request handling) as well as for building a service to service Envoy mesh (typically via
routing on the host/authority HTTP header to reach a particular upstream service cluster). Envoy
also has the ability to be configured as forward proxy. In the forward proxy configuration, mesh
clients can participate by appropriately configuring their http proxy to be an Envoy. At a high
level the router takes an incoming HTTP request, matches it to an upstream cluster, acquires a
:ref:`connection pool <arch_overview_conn_pool>` to a host in the upstream cluster, and forwards the
request. The router filter supports the following features:

* Virtual hosts that map domains/authorities to a set of routing rules.
* Prefix and exact path matching rules (both :ref:`case sensitive
  <envoy_api_field_route.RouteMatch.case_sensitive>` and case insensitive). Regex/slug
  matching is not currently supported, mainly because it makes it difficult/impossible to
  programmatically determine whether routing rules conflict with each other. For this reason we
  don’t recommend regex/slug routing at the reverse proxy level, however we may add support in the
  future depending on demand.
* :ref:`TLS redirection <envoy_api_field_route.VirtualHost.require_tls>` at the virtual host
  level.
* :ref:`Path <envoy_api_field_route.RedirectAction.path_redirect>`/:ref:`host
  <envoy_api_field_route.RedirectAction.host_redirect>` redirection at the route level.
* :ref:`Direct (non-proxied) HTTP responses <arch_overview_http_routing_direct_response>`
  at the route level.
* :ref:`Explicit host rewriting <envoy_api_field_route.RouteAction.host_rewrite>`.
* :ref:`Automatic host rewriting <envoy_api_field_route.RouteAction.auto_host_rewrite>` based on
  the DNS name of the selected upstream host.
* :ref:`Prefix rewriting <envoy_api_field_route.RedirectAction.prefix_rewrite>`.
* :ref:`Request retries <arch_overview_http_routing_retry>` specified either via HTTP header or via
  route configuration.
* Request timeout specified either via :ref:`HTTP
  header <config_http_filters_router_headers_consumed>` or via :ref:`route configuration
  <envoy_api_field_route.RouteAction.timeout>`.
* :ref:`Request hedging <arch_overview_http_routing_hedging>` for retries in response to a request (per try) timeout.
* Traffic shifting from one upstream cluster to another via :ref:`runtime values
  <envoy_api_field_route.RouteMatch.runtime_fraction>` (see :ref:`traffic shifting/splitting
  <config_http_conn_man_route_table_traffic_splitting>`).
* Traffic splitting across multiple upstream clusters using :ref:`weight/percentage-based routing
  <envoy_api_field_route.RouteAction.weighted_clusters>` (see :ref:`traffic shifting/splitting
  <config_http_conn_man_route_table_traffic_splitting_split>`).
* Arbitrary header matching :ref:`routing rules <envoy_api_msg_route.HeaderMatcher>`.
* Virtual cluster specifications. A virtual cluster is specified at the virtual host level and is
  used by Envoy to generate additional statistics on top of the standard cluster level ones. Virtual
  clusters can use regex matching.
* :ref:`Priority <arch_overview_http_routing_priority>` based routing.
* :ref:`Hash policy <envoy_api_field_route.RouteAction.hash_policy>` based routing.
* :ref:`Absolute urls <envoy_api_field_config.filter.network.http_connection_manager.v2.HttpConnectionManager.http_protocol_options>` are supported for non-tls forward proxies.

.. _arch_overview_http_routing_route_scope:

Route Scope
-----------

Scoped routing enables Envoy to put constraints on search space of domains and route rules.
A :ref:`Route Scope<envoy_api_msg_ScopedRouteConfiguration>` associates a key with a :ref:`route table <arch_overview_http_routing_route_table>`.
For each request, a scope key is computed dynamically by the HTTP connection manager to pick the :ref:`route table<envoy_api_msg_RouteConfiguration>`.

The Scoped RDS (SRDS) API contains a set of :ref:`Scopes <envoy_api_msg_ScopedRouteConfiguration>` resources, each defining independent routing configuration,
along with a :ref:`ScopeKeyBuilder <envoy_api_msg_config.filter.network.http_connection_manager.v2.ScopedRoutes.ScopeKeyBuilder>`
defining the key construction algorithm used by Envoy to look up the scope corresponding to each request.

For example, for the following scoped route configuration, Envoy will look into the "addr" header value, split the header value by ";" first, and use the first value for key 'x-foo-key' as the scope key.
If the "addr" header value is "foo=1;x-foo-key=127.0.0.1;x-bar-key=1.1.1.1", then "127.0.0.1" will be computed as the scope key to look up for corresponding route configuration.

.. code-block:: yaml

  name: scope_by_addr
  fragments:
    - header_value_extractor:
        name: Addr
        element_separator: ;
        element:
          key: x-foo-key
          separator: =

.. _arch_overview_http_routing_route_table:

Route table
-----------

The :ref:`configuration <config_http_conn_man>` for the HTTP connection manager owns the :ref:`route
table <envoy_api_msg_RouteConfiguration>` that is used by all configured HTTP filters. Although the
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
<envoy_api_field_route.RouteAction.retry_policy>` as well as for specific requests via :ref:`request
headers <config_http_filters_router_headers_consumed>`. The following configurations are possible:

* **Maximum number of retries**: Envoy will continue to retry any number of times. An exponential
  backoff algorithm is used between each retry. Additionally, *all retries are contained within the
  overall request timeout*. This avoids long request times due to a large number of retries.
* **Retry conditions**: Envoy can retry on different types of conditions depending on application
  requirements. For example, network failure, all 5xx response codes, idempotent 4xx response codes,
  etc.
* **Host selection retry plugins**: Envoy can be configured to apply additional logic to the host
  selection logic when selecting hosts for retries. Specifying a
  :ref:`retry host predicate <envoy_api_field_route.RetryPolicy.retry_host_predicate>`
  allows for reattempting host selection when certain hosts are selected (e.g. when an already
  attempted host is selected), while a
  :ref:`retry priority <envoy_api_field_route.RetryPolicy.retry_priority>` can be
  configured to adjust the priority load used when selecting a priority for retries.

Note that retries may be disabled depending on the contents of the :ref:`x-envoy-overloaded
<config_http_filters_router_x-envoy-overloaded_consumed>`.

.. _arch_overview_http_routing_hedging:

Request Hedging
---------------

Envoy supports request hedging which can be enabled by specifying a :ref:`hedge
policy <envoy_api_msg_route.HedgePolicy>`. This means that Envoy will race
multiple simultaneous upstream requests and return the response associated with
the first acceptable response headers to the downstream. The retry policy is
used to determine whether a response should be returned or whether more
responses should be awaited.

Currently hedging can only be performed in response to a request timeout. This
means that a retry request will be issued without canceling the initial
timed-out request and a late response will be awaited. The first "good"
response according to retry policy will be returned downstream.

The implementation ensures that the same upstream request is not retried twice.
This might otherwise occur if a request times out and then results in a 5xx
response, creating two retriable events.

.. _arch_overview_http_routing_priority:

Priority routing
----------------

Envoy supports priority routing at the :ref:`route <envoy_api_msg_route.Route>` level.
The current priority implementation uses different :ref:`connection pool <arch_overview_conn_pool>`
and :ref:`circuit breaking <config_cluster_manager_cluster_circuit_breakers>` settings for each
priority level. This means that even for HTTP/2 requests, two physical connections will be used to
an upstream host. In the future Envoy will likely support true HTTP/2 priority over a single
connection.

The currently supported priorities are *default* and *high*.

.. _arch_overview_http_routing_direct_response:

Direct responses
----------------

Envoy supports the sending of "direct" responses. These are preconfigured HTTP responses
that do not require proxying to an upstream server.

There are two ways to specify a direct response in a Route:

* Set the :ref:`direct_response <envoy_api_field_route.Route.direct_response>` field.
  This works for all HTTP response statuses.
* Set the :ref:`redirect <envoy_api_field_route.Route.redirect>` field. This works for
  redirect response statuses only, but it simplifies the setting of the *Location* header.

A direct response has an HTTP status code and an optional body. The Route configuration
can specify the response body inline or specify the pathname of a file containing the
body. If the Route configuration specifies a file pathname, Envoy will read the file
upon configuration load and cache the contents.

.. attention::

   If a response body is specified, it must be no more than 4KB in size, regardless of
   whether it is provided inline or in a file. Envoy currently holds the entirety of the
   body in memory, so the 4KB limit is intended to keep the proxy's memory footprint
   from growing too large.

If **response_headers_to_add** has been set for the Route or the enclosing Virtual Host,
Envoy will include the specified headers in the direct HTTP response.
