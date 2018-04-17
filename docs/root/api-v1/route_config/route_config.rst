.. _config_http_conn_man_route_table:

HTTP Route configuration
========================

* Routing :ref:`architecture overview <arch_overview_http_routing>`
* HTTP :ref:`router filter <config_http_filters_router>`

.. code-block:: json

  {
    "validate_clusters": "...",
    "virtual_hosts": [],
    "internal_only_headers": [],
    "response_headers_to_add": [],
    "response_headers_to_remove": [],
    "request_headers_to_add": []
  }

.. _config_http_conn_man_route_table_validate_clusters:

validate_clusters
  *(optional, boolean)* An optional boolean that specifies whether the clusters that the route
  table refers to will be validated by the cluster manager. If set to true and a route refers to
  a non-existent cluster, the route table will not load. If set to false and a route refers to a
  non-existent cluster, the route table will load and the router filter will return a 404 if the
  route is selected at runtime. This setting defaults to true if the route table is statically
  defined via the :ref:`route_config <config_http_conn_man_route_config>` option. This setting
  default to false if the route table is loaded dynamically via the :ref:`rds
  <config_http_conn_man_rds_option>` option. Users may which to override the default behavior in
  certain cases (for example when using :ref:`cds <config_cluster_manager_cds_v1>` with a static
  route table).

:ref:`virtual_hosts <config_http_conn_man_route_table_vhost>`
  *(required, array)* An array of virtual hosts that make up the route table.

internal_only_headers
  *(optional, array)* Optionally specifies a list of HTTP headers that the connection manager
  will consider to be internal only. If they are found on external requests they will be cleaned
  prior to filter invocation. See :ref:`config_http_conn_man_headers_x-envoy-internal` for more
  information. Headers are specified in the following form:

  .. code-block:: json

    ["header1", "header2"]

response_headers_to_add
  *(optional, array)* Optionally specifies a list of HTTP headers that should be added to each
  response that the connection manager encodes. Headers are specified in the following form:

  .. code-block:: json

    [
      {"key": "header1", "value": "value1"},
      {"key": "header2", "value": "value2"}
    ]

  For more information, including details on header value syntax, see the documentation on
  :ref:`custom request headers <config_http_conn_man_headers_custom_request_headers>`.

response_headers_to_remove
  *(optional, array)* Optionally specifies a list of HTTP headers that should be removed from each
  response that the connection manager encodes. Headers are specified in the following form:

  .. code-block:: json

    ["header1", "header2"]

.. _config_http_conn_man_route_table_add_req_headers:

request_headers_to_add
  *(optional, array)* Specifies a list of HTTP headers that should be added to each
  request forwarded by the HTTP connection manager. Headers are specified in the following form:

  .. code-block:: json

    [
      {"key": "header1", "value": "value1"},
      {"key": "header2", "value": "value2"}
    ]

  For more information, including details on header value syntax, see the documentation on
  :ref:`custom request headers <config_http_conn_man_headers_custom_request_headers>`.

.. toctree::
  :hidden:

  vhost
  route
  vcluster
  rate_limits
  rds
