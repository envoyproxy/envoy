.. _config_http_conn_man_route_table_route:

Route
=====

A route is both a specification of how to match a request as well as in indication of what to do
next (e.g., redirect, forward, rewrite, etc.).

.. attention::

  Envoy supports routing on HTTP method via :ref:`header matching
  <config_http_conn_man_route_table_route_headers>`.

.. code-block:: json

  {
    "prefix": "...",
    "path": "...",
    "regex": "...",
    "cluster": "...",
    "cluster_header": "...",
    "weighted_clusters" : "{...}",
    "host_redirect": "...",
    "path_redirect": "...",
    "prefix_rewrite": "...",
    "host_rewrite": "...",
    "auto_host_rewrite": "...",
    "case_sensitive": "...",
    "use_websocket": "...",
    "timeout_ms": "...",
    "runtime": "{...}",
    "retry_policy": "{...}",
    "shadow": "{...}",
    "priority": "...",
    "headers": [],
    "rate_limits": [],
    "include_vh_rate_limits" : "...",
    "hash_policy": "{...}",
    "request_headers_to_add" : [],
    "opaque_config": [],
    "cors": "{...}",
    "decorator" : "{...}"
  }

prefix
  *(sometimes required, string)* If specified, the route is a prefix rule meaning that the prefix
  must match the beginning of the :path header. One of *prefix*, *path*, or *regex* must be specified.

path
  *(sometimes required, string)* If specified, the route is an exact path rule meaning that the path
  must exactly match the :path header once the query string is removed. One of *prefix*, *path*, or
  *regex* must be specified.

regex
  *(sometimes required, string)* If specified, the route is a regular expression rule meaning that the
  regex must match the :path header once the query string is removed. The entire path (without the
  query string) must match the regex. The rule will not match if only a subsequence of the :path header
  matches the regex. The regex grammar is defined `here
  <http://en.cppreference.com/w/cpp/regex/ecmascript>`_. One of *prefix*, *path*, or
  *regex* must be specified.

  Examples:

    * The regex */b[io]t* matches the path */bit*
    * The regex */b[io]t* matches the path */bot*
    * The regex */b[io]t* does not match the path */bite*
    * The regex */b[io]t* does not match the path */bit/bot*

:ref:`cors <config_http_conn_man_route_table_cors>`
  *(optional, object)* Specifies the route's CORS policy.

.. _config_http_conn_man_route_table_route_cluster:

cluster
  *(sometimes required, string)* If the route is not a redirect (*host_redirect* and/or
  *path_redirect* is not specified), one of *cluster*, *cluster_header*, or *weighted_clusters* must
  be specified. When *cluster* is specified, its value indicates the upstream cluster to which the
  request should be forwarded to.

.. _config_http_conn_man_route_table_route_cluster_header:

cluster_header
  *(sometimes required, string)* If the route is not a redirect (*host_redirect* and/or
  *path_redirect* is not specified), one of *cluster*, *cluster_header*, or *weighted_clusters* must
  be specified. When *cluster_header* is specified, Envoy will determine the cluster to route to
  by reading the value of the HTTP header named by *cluster_header* from the request headers.
  If the header is not found or the referenced cluster does not exist, Envoy will return a 404
  response.

  .. attention::

    Internally, Envoy always uses the HTTP/2 *:authority* header to represent the HTTP/1 *Host*
    header. Thus, if attempting to match on *Host*, match on *:authority* instead.

.. _config_http_conn_man_route_table_route_config_weighted_clusters:

:ref:`weighted_clusters <config_http_conn_man_route_table_route_weighted_clusters>`
  *(sometimes required, object)* If the route is not a redirect (*host_redirect* and/or
  *path_redirect* is not specified), one of *cluster*, *cluster_header*, or *weighted_clusters* must
  be specified. With the *weighted_clusters* option, multiple upstream clusters can be specified for
  a given route. The request is forwarded to one of the upstream clusters based on weights assigned
  to each cluster. See :ref:`traffic splitting <config_http_conn_man_route_table_traffic_splitting_split>`
  for additional documentation.

.. _config_http_conn_man_route_table_route_host_redirect:

host_redirect
  *(sometimes required, string)* Indicates that the route is a redirect rule. If there is a match,
  a 301 redirect response will be sent which swaps the host portion of the URL with this value.
  *path_redirect* can also be specified along with this option.

.. _config_http_conn_man_route_table_route_path_redirect:

path_redirect
  *(sometimes required, string)* Indicates that the route is a redirect rule. If there is a match,
  a 301 redirect response will be sent which swaps the path portion of the URL with this value.
  *host_redirect*  can also be specified along with this option.

.. _config_http_conn_man_route_table_route_prefix_rewrite:

prefix_rewrite
  *(optional, string)* Indicates that during forwarding, the matched prefix (or path) should be
  swapped with this value. When using regex path matching, the entire path (not including
  the query string) will be swapped with this value. This option allows application URLs to be
  rooted at a different path from those exposed at the reverse proxy layer. The router filter will
  place the original path before rewrite into the :ref:`x-envoy-original-path
  <config_http_filters_router_x-envoy-original-path>` header.

.. _config_http_conn_man_route_table_route_host_rewrite:

host_rewrite
  *(optional, string)* Indicates that during forwarding, the host header will be swapped with this
  value.

.. _config_http_conn_man_route_table_route_auto_host_rewrite:

auto_host_rewrite
  *(optional, boolean)* Indicates that during forwarding, the host header will be swapped with the
  hostname of the upstream host chosen by the cluster manager. This option is applicable only when
  the destination cluster for a route is of type *strict_dns* or *logical_dns*. Setting this to true
  with other cluster types has no effect. *auto_host_rewrite* and *host_rewrite* are mutually exclusive
  options. Only one can be specified.

.. _config_http_conn_man_route_table_route_case_sensitive:

case_sensitive
  *(optional, boolean)* Indicates that prefix/path matching should be case sensitive. The default
  is true.

.. _config_http_conn_man_route_table_route_use_websocket:

use_websocket
  *(optional, boolean)* Indicates that a HTTP/1.1 client connection to this particular route
  should be allowed to upgrade to a WebSocket connection. The default is false.

  .. attention::

    If set to true, Envoy will expect the first request matching this route to contain WebSocket
    upgrade headers. If the headers are not present, the connection will be processed as a normal
    HTTP/1.1 connection. If the upgrade headers are present, Envoy will setup plain TCP proxying
    between the client and the upstream server. Hence, an upstream server that rejects the WebSocket
    upgrade request is also responsible for closing the associated connection. Until then, Envoy will
    continue to proxy data from the client to the upstream server.

    Redirects, timeouts and retries are not supported on requests with WebSocket upgrade headers.

.. _config_http_conn_man_route_table_route_timeout:

timeout_ms
  *(optional, integer)* Specifies the timeout for the route. If not specified, the default is 15s.
  Note that this timeout includes all retries. See also
  :ref:`config_http_filters_router_x-envoy-upstream-rq-timeout-ms`,
  :ref:`config_http_filters_router_x-envoy-upstream-rq-per-try-timeout-ms`, and the
  :ref:`retry overview <arch_overview_http_routing_retry>`.

:ref:`runtime <config_http_conn_man_route_table_route_runtime>`
  *(optional, object)* Indicates that the route should additionally match on a runtime key.

:ref:`retry_policy <config_http_conn_man_route_table_route_retry>`
  *(optional, object)* Indicates that the route has a retry policy.

:ref:`shadow <config_http_conn_man_route_table_route_shadow>`
  *(optional, object)* Indicates that the route has a shadow policy.

priority
  *(optional, string)* Optionally specifies the :ref:`routing priority
  <arch_overview_http_routing_priority>`.

:ref:`headers <config_http_conn_man_route_table_route_headers>`
  *(optional, array)* Specifies a set of headers that the route should match on. The router will
  check the request's headers against all the specified headers in the route config. A match will
  happen if all the headers in the route are present in the request with the same values (or based
  on presence if the ``value`` field is not in the config).

request_headers_to_add
  *(optional, array)* Specifies a list of HTTP headers that should be added to each
  request handled by this virtual host. Headers are specified in the following form:

  .. code-block:: json

    [
      {"key": "header1", "value": "value1"},
      {"key": "header2", "value": "value2"}
    ]

  For more information see the documentation on :ref:`custom request headers
  <config_http_conn_man_headers_custom_request_headers>`.

:ref:`opaque_config <config_http_conn_man_route_table_opaque_config>`
  *(optional, array)* Specifies a set of optional route configuration values that can be accessed by filters.

.. _config_http_conn_man_route_table_route_rate_limits:

:ref:`rate_limits <config_http_conn_man_route_table_rate_limit_config>`
  *(optional, array)* Specifies a set of rate limit configurations that could be applied to the
  route.

.. _config_http_conn_man_route_table_route_include_vh:

include_vh_rate_limits
  *(optional, boolean)* Specifies if the rate limit filter should include the virtual host rate
  limits. By default, if the route configured rate limits, the virtual host
  :ref:`rate_limits <config_http_conn_man_route_table_rate_limit_config>` are not applied to the
  request.

:ref:`hash_policy <config_http_conn_man_route_table_hash_policy>`
  *(optional, object)* Specifies the route's hashing policy if the upstream cluster uses a hashing
  :ref:`load balancer <arch_overview_load_balancing_types>`.

:ref:`decorator <config_http_conn_man_route_table_decorator>`
  *(optional, object)* Specifies the route's decorator used to enhance information reported about
  the matched request.

.. _config_http_conn_man_route_table_route_runtime:

Runtime
-------

A :ref:`runtime <arch_overview_runtime>` route configuration can be used to roll out route changes
in a gradual manner without full code/config deploys. Refer to the
:ref:`traffic shifting <config_http_conn_man_route_table_traffic_splitting_shift>` docs
for additional documentation.

.. code-block:: json

  {
    "key": "...",
    "default": "..."
  }

key
  *(required, string)* Specifies the runtime key name that should be consulted to determine whether
  the route matches or not. See the :ref:`runtime documentation <operations_runtime>` for how key
  names map to the underlying implementation.

.. _config_http_conn_man_route_table_route_runtime_default:

default
  *(required, integer)* An integer between 0-100. Every time the route is considered for a match,
  a random number between 0-99 is selected. If the number is <= the value found in the *key*
  (checked first) or, if the key is not present, the default value, the route is a match (assuming
  everything also about the route matches).

.. _config_http_conn_man_route_table_route_retry:

Retry policy
------------

HTTP retry :ref:`architecture overview <arch_overview_http_routing_retry>`.

.. code-block:: json

  {
    "retry_on": "...",
    "num_retries": "...",
    "per_try_timeout_ms" : "..."
  }

retry_on
  *(required, string)* Specifies the conditions under which retry takes place. These are the same
  conditions documented for :ref:`config_http_filters_router_x-envoy-retry-on` and
  :ref:`config_http_filters_router_x-envoy-retry-grpc-on`.

num_retries
  *(optional, integer)* Specifies the allowed number of retries. This parameter is optional and
  defaults to 1. These are the same conditions documented for
  :ref:`config_http_filters_router_x-envoy-max-retries`.

per_try_timeout_ms
  *(optional, integer)* Specifies a non-zero timeout per retry attempt. This parameter is optional.
  The same conditions documented for
  :ref:`config_http_filters_router_x-envoy-upstream-rq-per-try-timeout-ms` apply.

  **Note:** If left unspecified, Envoy will use the global
  :ref:`route timeout <config_http_conn_man_route_table_route_timeout>` for the request.
  Consequently, when using a :ref:`5xx <config_http_filters_router_x-envoy-retry-on>` based
  retry policy, a request that times out will not be retried as the total timeout budget
  would have been exhausted.

.. _config_http_conn_man_route_table_route_shadow:

Shadow
------

The router is capable of shadowing traffic from one cluster to another. The current implementation
is "fire and forget," meaning Envoy will not wait for the shadow cluster to respond before returning
the response from the primary cluster. All normal statistics are collected for the shadow
cluster making this feature useful for testing.

During shadowing, the host/authority header is altered such that *-shadow* is appended. This is
useful for logging. For example, *cluster1* becomes *cluster1-shadow*.

.. code-block:: json

  {
    "cluster": "...",
    "runtime_key": "..."
  }

cluster
  *(required, string)* Specifies the cluster that requests will be shadowed to. The cluster must
  exist in the :ref:`cluster manager configuration <config_cluster_manager>`.

runtime_key
  *(optional, string)* If not specified, **all** requests to the target cluster will be shadowed.
  If specified, Envoy will lookup the runtime key to get the % of requests to shadow. Valid values are
  from 0 to 10000, allowing for increments of 0.01% of requests to be shadowed. If the runtime key
  is specified in the configuration but not present in runtime, 0 is the default and thus 0% of
  requests will be shadowed.

.. _config_http_conn_man_route_table_route_headers:

Headers
-------

.. code-block:: json

  {
    "name": "...",
    "value": "...",
    "regex": "...",
    "range_match": "..."
  }

name
  *(required, string)* Specifies the name of the header in the request.

value
  *(optional, string)* Specifies the value of the header. If the value is absent a request that has
  the *name* header will match, regardless of the header's value.

regex
  *(optional, boolean)* Specifies whether the header value is a regular
  expression or not. Defaults to false. The entire request header value must match the regex. The
  rule will not match if only a subsequence of the request header value matches the regex. The
  regex grammar used in the value field is defined
  `here <http://en.cppreference.com/w/cpp/regex/ecmascript>`_.

  Examples:

    * The regex *\d{3}* matches the value *123*
    * The regex *\d{3}* does not match the value *1234*
    * The regex *\d{3}* does not match the value *123.456*

:ref:`range_match <config_http_conn_man_route_table_range>`
  *(optional, object)* Specifies the range that will be used for header matching.

.. attention::

  Internally, Envoy always uses the HTTP/2 *:authority* header to represent the HTTP/1 *Host*
  header. Thus, if attempting to match on *Host*, match on *:authority* instead.

.. attention::

  To route on HTTP method, use the special HTTP/2 *:method* header. This works for both
  HTTP/1 and HTTP/2 as Envoy normalizes headers. E.g.,

  .. code-block:: json

    {
      "name": ":method",
      "value": "POST"
    }

.. _config_http_conn_man_route_table_route_weighted_clusters:

Weighted Clusters
-----------------

Compared to the ``cluster`` field that specifies a single upstream cluster as the target
of a request, the ``weighted_clusters`` option allows for specification of multiple upstream clusters
along with weights that indicate the **percentage** of traffic to be forwarded to each cluster.
The router selects an upstream cluster based on the weights.

.. code-block:: json

   {
     "clusters": [],
     "runtime_key_prefix" : "..."
   }

clusters
  *(required, array)* Specifies one or more upstream clusters associated with the route.

  .. code-block:: json

     {
       "name" : "...",
       "weight": "..."
     }

  name
    *(required, string)* Name of the upstream cluster. The cluster must exist in the
    :ref:`cluster manager configuration <config_cluster_manager>`.

  weight
    *(required, integer)* An integer between 0-100. When a request matches the route,
    the choice of an upstream cluster is determined by its weight. The sum of
    weights across all entries in the *clusters* array must add up to 100.

runtime_key_prefix
  *(optional, string)* Specifies the runtime key prefix that should be used to construct the runtime
  keys associated with each cluster. When the ``runtime_key_prefix`` is specified, the router will
  look for weights associated with each upstream cluster under the key
  ``runtime_key_prefix + "." + cluster[i].name`` where ``cluster[i]``  denotes an entry in the
  ``clusters`` array field. If the runtime key for the cluster does not exist, the value specified
  in the configuration file will be used as the default weight.
  See the :ref:`runtime documentation <operations_runtime>` for how key names map to the
  underlying implementation.

  **Note:** If the sum of runtime weights exceed 100, the traffic splitting behavior
  is undefined (although the request will be routed to one of the clusters).

.. _config_http_conn_man_route_table_hash_policy:

Hash policy
-----------

Specifies the route's hashing policy if the upstream cluster uses a hashing :ref:`load balancer
<arch_overview_load_balancing_types>`.

.. code-block:: json

   {
     "header_name": "..."
   }

header_name
  *(required, string)* The name of the request header that will be used to obtain the hash key. If
  the request header is not present, the load balancer will use a random number as the hash,
  effectively making the load balancing policy random.

.. _config_http_conn_man_route_table_decorator:

Decorator
---------

Specifies the route's decorator.

.. code-block:: json

   {
     "operation": "..."
   }

operation
  *(required, string)* The operation name associated with the request matched to this route. If tracing is
  enabled, this information will be used as the span name reported for this request. NOTE: For ingress
  (inbound) requests, or egress (outbound) responses, this value may be overridden by the
  :ref:`x-envoy-decorator-operation <config_http_filters_router_x-envoy-decorator-operation>` header.

.. _config_http_conn_man_route_table_opaque_config:

Opaque Config
-------------

Additional configuration can be provided to filters through the "Opaque Config" mechanism. A
list of properties are specified in the route config. The configuration is uninterpreted
by envoy and can be accessed within a user-defined filter. The configuration is a generic
string map. Nested objects are not supported.

.. code-block:: json

  [
    {"...": "..."}
  ]

.. _config_http_conn_man_route_table_cors:

Cors
--------

Settings on a route take precedence over settings on the virtual host.

.. code-block:: json

  {
    "enabled": false,
    "allow_origin": ["http://foo.example"],
    "allow_methods": "POST, GET, OPTIONS",
    "allow_headers": "Content-Type",
    "allow_credentials": false,
    "expose_headers": "X-Custom-Header",
    "max_age": "86400"
  }

enabled
  *(optional, boolean)* Defaults to true. Setting *enabled* to false on a route disables CORS
  for this route only. The setting has no effect on a virtual host.

allow_origin
  *(optional, array)* The origins that will be allowed to do CORS request.
  Wildcard "\*" will allow any origin.

allow_methods
  *(optional, string)* The content for the *access-control-allow-methods* header.
  Comma separated list of HTTP methods.

allow_headers
  *(optional, string)* The content for the *access-control-allow-headers* header.
  Comma separated list of HTTP headers.

allow_credentials
  *(optional, boolean)* Whether the resource allows credentials.

expose_headers
  *(optional, string)* The content for the *access-control-expose-headers* header.
  Comma separated list of HTTP headers.

max_age
  *(optional, string)* The content for the *access-control-max-age* header.
  Value in seconds for how long the response to the preflight request can be cached.

  .. _config_http_conn_man_route_table_range:

range_match
--------------

Specifies the int64 start and end of the range using half-open interval semantics [start, end).
Header route matching will be performed if the header's value lies within this range.

.. code-block:: json

  {
    "start": "...",
    "end": "..."
  }

start
  *(required, integer)* start of the range (inclusive).

end
  *(required, integer)* end of the range (exclusive).
