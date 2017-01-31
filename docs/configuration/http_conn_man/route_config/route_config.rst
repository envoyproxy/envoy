.. _config_http_conn_man_route_table:

Route configuration
===================

* Routing :ref:`architecture overview <arch_overview_http_routing>`.
* HTTP :ref:`router filter <config_http_filters_router>`.

.. code-block:: json

  {
    "virtual_hosts": [],
    "internal_only_headers": [],
    "response_headers_to_add": [],
    "response_headers_to_remove": []
  }

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

response_headers_to_remove
  *(optional, array)* Optionally specifies a list of HTTP headers that should be removed from each
  response that the connection manager encodes. Headers are specified in the following form:

  .. code-block:: json

    ["header1", "header2"]

.. toctree::
  :hidden:

  vhost
  route
  vcluster
  rate_limits
  route_matching
  traffic_splitting
