.. _config_http_conn_man_route_table_vcluster:

Virtual cluster
===============

A virtual cluster is a way of specifying a regex matching rule against certain important endpoints
such that statistics are generated explicitly for the matched requests. The reason this is useful is
that when doing prefix/path matching Envoy does not always know what the application considers to
be an endpoint. Thus, it's impossible for Envoy to generically emit per endpoint statistics.
However, often systems have highly critical endpoints that they wish to get "perfect" statistics on.
Virtual cluster statistics are perfect in the sense that they are emitted on the downstream side
such that they include network level failures.

.. note::

  Virtual clusters are a useful tool, but we do not recommend setting up a virtual cluster for
  every application endpoint. This is both not easily maintainable as well as the matching and
  statistics output are not free.

.. code-block:: json

  {
    "pattern": "...",
    "name": "...",
    "method": "..."
  }

pattern
  *(required, string)* Specifies a regex pattern to use for matching requests. The entire path of the request
  must match the regex. The regex grammar used is defined `here <http://en.cppreference.com/w/cpp/regex/ecmascript>`_.

name
  *(required, string)* Specifies the name of the virtual cluster. The virtual cluster name as well
  as the virtual host name are used when emitting statistics. The statistics are emitted by the
  router filter and are documented :ref:`here <config_http_filters_router_stats>`.

method
  *(optional, string)* Optionally specifies the HTTP method to match on. For example *GET*, *PUT*,
  etc.

  Examples:

    * The regex */rides/\d+* matches the path */rides/0*
    * The regex */rides/\d+* matches the path */rides/123*
    * The regex */rides/\d+* does not match the path */rides/123/456*

Documentation for :ref:`virtual cluser statistics <config_http_filters_router_stats>`.
