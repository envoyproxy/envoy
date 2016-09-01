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
    "host_redirect": "...",
    "path_redirect": "...",
    "prefix_rewrite": "...",
    "host_rewrite": "...",
    "case_sensitive": "...",
    "timeout_ms": "...",
    "content_type": "...",
    "runtime": "{...}",
    "retry_policy": "{...}",
    "rate_limit": "{...}",
    "shadow": "{...}"
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
  *path_redirect* is specified), *cluster* must be specified and indicates which upstream cluster
  the request should be forwarded to.

host_redirect
  *(sometimes required, string)* Indicates that the route is a redirect rule. If there is a match,
  A 302 redirect response will be sent which swaps the host portion of the URL with this value.
  *path_redirect* can also be specified along with this option.

path_redirect
  *(sometimes required, string)* Indicates that the route is a redirect rule. If there is a match,
  A 302 redirect response will be sent which swaps the path portion of the URL with this value.
  *host_redirect*  can also be specified along with this option.

prefix_rewrite
  *(optional, string)* Indicates that during forwarding, the matched prefix (or path) should be
  swapped with this value. This option allows application URLs to be rooted at a different path
  from those exposed at the reverse proxy layer.

host_rewrite
  *(optional, string)* Indicates that during forwarding, the host header will be swapped with this
  value.

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

content_type
  *(optional, string)* Indicates that the route should additionally match on the *content-type*
  HTTP header.

:ref:`runtime <config_http_conn_man_route_table_route_runtime>`
  *(optional, object)* Indicates that the route should additionally match on a runtime key.

:ref:`retry_policy <config_http_conn_man_route_table_route_retry>`
  *(optional, object)* Indicates that the route has a retry policy.

:ref:`rate_limit <config_http_conn_man_route_table_route_rate_limit>`
  *(optional, object)* Indicates that the route has a rate limit policy.

:ref:`shadow <config_http_conn_man_route_table_route_shadow>`
  *(optional, object)* Indicates that the route has a shadow policy.

.. _config_http_conn_man_route_table_route_runtime:

Runtime
-------

A :ref:`runtime <arch_overview_runtime>` route configuration can be used to roll out route changes
in a gradual manner without full code/config deploys.

.. code-block:: json

  {
    "key": "...",
    "default": "..."
  }

key
  *(required, string)* Specifies the runtime key name that should be consulted to determine whether
  the route matches or not. See the :ref:`runtime documentation <operations_runtime>` for how key
  names map to the underlying implementation.

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

.. _config_http_conn_man_route_table_route_rate_limit:

Rate limit
----------

Global rate limit :ref:`architecture overview <arch_overview_rate_limit>`.

.. code-block:: json

  {
    "global": "..."
  }

global
  *(optional, boolean)* Specifies whether the global rate limit service should be called for a
  request that matches this route. This information is used by the :ref:`rate limit filter
  <config_http_filters_rate_limit>` if it is installed. Defaults to false if not specified.

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
