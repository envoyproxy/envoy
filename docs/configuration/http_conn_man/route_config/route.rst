.. _config_http_conn_man_route_table_route:

Route
=====

A route is both a specification of how to match a request as well as in indication of what to do
next (e.g., redirect, forward, rewrite, etc.).

.. code-block:: json

  {
    "prefix": "...",
    "path": "...",
    "cluster": "...",
    "cluster_header": "...",
    "weighted_clusters" : "{...}",
    "host_redirect": "...",
    "path_redirect": "...",
    "prefix_rewrite": "...",
    "host_rewrite": "...",
    "auto_host_rewrite": "...",
    "case_sensitive": "...",
    "timeout_ms": "...",
    "runtime": "{...}",
    "retry_policy": "{...}",
    "shadow": "{...}",
    "priority": "...",
    "headers": [],
    "rate_limits": [],
    "exclude_vh_rate_limits" : "...",
    "hash_policy": "{...}",
    "request_headers_to_add" : [],
    "opaque_config": []
  }

prefix
  *(sometimes required, string)* If specified, the route is a prefix rule meaning that the prefix
  must match the beginning of the :path header. Either *prefix* or *path* must be specified.

path
  *(sometimes required, string)* If specified, the route is an exact path rule meaning that the path
  must exactly match the :path header once the query string is removed. Either *prefix* or *path*
  must be specified.

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
  a 302 redirect response will be sent which swaps the host portion of the URL with this value.
  *path_redirect* can also be specified along with this option.

.. _config_http_conn_man_route_table_route_path_redirect:

path_redirect
  *(sometimes required, string)* Indicates that the route is a redirect rule. If there is a match,
  a 302 redirect response will be sent which swaps the path portion of the URL with this value.
  *host_redirect*  can also be specified along with this option.

.. _config_http_conn_man_route_table_route_prefix_rewrite:

prefix_rewrite
  *(optional, string)* Indicates that during forwarding, the matched prefix (or path) should be
  swapped with this value. This option allows application URLs to be rooted at a different path
  from those exposed at the reverse proxy layer.

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
  *(optional, string)* Indicates that prefix/path matching should be case sensitive. The default
  is true.

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

:ref:`request_headers_to_add <config_http_conn_man_route_table_route_add_req_headers>`
  *(optional, array)* Specifies a set of headers that will be added to requests matching this route.

:ref:`opaque_config <config_http_conn_man_route_table_opaque_config>`
  *(optional, array)* Specifies a set of optional route configuration values that can be accessed by filters.

.. _config_http_conn_man_route_table_route_rate_limits:

:ref:`rate_limits <config_http_conn_man_route_table_rate_limit_config>`
  *(optional, array)* Specifies a set of rate limit configurations that could be applied to the
  route.

.. _config_http_conn_man_route_table_route_exclude_vh:

exclude_vh_rate_limits
  *(optional, boolean)* Specifies if the virtual host rate limits should be excluded by the rate
  limit filter. The default value is true if
  :ref:`rate_limits <config_http_conn_man_route_table_rate_limit_config>` is specified for the route.

:ref:`hash_policy <config_http_conn_man_route_table_hash_policy>`
  *(optional, array)* Specifies the route's hashing policy if the upstream cluster uses a hashing
  :ref:`load balancer <arch_overview_load_balancing_types>`.

.. _config_http_conn_man_route_table_route_runtime:

Runtime
-------

A :ref:`runtime <arch_overview_runtime>` route configuration can be used to roll out route changes
in a gradual manner without full code/config deploys. Refer to
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
    "num_retries": "..."
  }

retry_on
  *(required, string)* specifies the conditions under which retry takes place. These are the same
  conditions documented for :ref:`config_http_filters_router_x-envoy-retry-on`.

num_retries
  *(optional, integer)* specifies the allowed number of retries. This parameter is optional and
  defaults to 1. These are the same conditions documented for
  :ref:`config_http_filters_router_x-envoy-max-retries`.

.. _config_http_conn_man_route_table_route_shadow:

Shadow
------

The router is capable of shadowing traffic from one cluster to another. The current implementation
is "fire and forget," meaning Envoy will not wait for the shadow cluster to respond before returning
the response from the primary cluster. All normal statistics are collected however for the shadow
cluster making thie feature useful for testing.

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
    "regex": "..."
  }

name
  *(required, string)* Specifies the name of the header in the request.

value
  *(optional, string)* Specifies the value of the header. If the value is absent a request that has
  the *name* header will match, regardless of the header's value.

regex
  *(optional, boolean)* Specifies whether the header value is a regular
  expression or not. Defaults to false. The regex grammar used in the value field
  is defined `here <http://en.cppreference.com/w/cpp/regex/ecmascript>`_.

.. attention::

  Internally, Envoy always uses the HTTP/2 *:authority* header to represent the HTTP/1 *Host*
  header. Thus, if attempting to match on *Host*, match on *:authority* instead.

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
    weights across all entries in the ``clusters`` array must add up to 100.

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

.. _config_http_conn_man_route_table_route_add_req_headers:

Adding custom request headers
-----------------------------

Custom request headers can be added to a request that matches a specific route. The headers are
specified in the following form:

.. code-block:: json

  [
    {"key": "header1", "value": "value1"},
    {"key": "header2", "value": "value2"}
  ]

*Note:* Headers are appended to requests in the following order:
route-level headers, :ref:`virtual host level <config_http_conn_man_route_table_vhost_add_req_headers>`
headers and finally global :ref:`route_config <config_http_conn_man_route_table_add_req_headers>`
level headers.

.. _config_http_conn_man_route_table_opaque_config:

Opaque Config
-------------

Additional configuration can be provided to filters through the "Opaque Config" mechanism. A
list of properties are specified in the route config. The configuration is uninterpreted
by envoy and can be accessed within a user-defined filter. The configuration is a generic
string map.  Nested objects are not supported.

.. code-block:: json

  [
    {"...": "..."}
  ]
