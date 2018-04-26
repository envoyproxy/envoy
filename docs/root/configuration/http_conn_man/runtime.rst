.. _config_http_conn_man_runtime:

Runtime
=======

The HTTP connection manager supports the following runtime settings:

.. _config_http_conn_man_runtime_represent_ipv4_remote_address_as_ipv4_mapped_ipv6:

http_connection_manager.represent_ipv4_remote_address_as_ipv4_mapped_ipv6
  % of requests with a remote address that will have their IPv4 address mapped to IPv6. Defaults to
  0.
  :ref:`use_remote_address <envoy_api_field_config.filter.network.http_connection_manager.v2.HttpConnectionManager.use_remote_address>`
  must also be enabled. See
  :ref:`represent_ipv4_remote_address_as_ipv4_mapped_ipv6
  <envoy_api_field_config.filter.network.http_connection_manager.v2.HttpConnectionManager.represent_ipv4_remote_address_as_ipv4_mapped_ipv6>`
  for more details.

.. _config_http_conn_man_runtime_client_enabled:

tracing.client_enabled
  % of requests that will be force traced if the
  :ref:`config_http_conn_man_headers_x-client-trace-id` header is set. Defaults to 100.

.. _config_http_conn_man_runtime_global_enabled:

tracing.global_enabled
  % of requests that will be traced after all other checks have been applied (force tracing,
  sampling, etc.). Defaults to 100.

.. _config_http_conn_man_runtime_random_sampling:

tracing.random_sampling
  % of requests that will be randomly traced. See :ref:`here <arch_overview_tracing>` for more
  information. This runtime control is specified in the range 0-10000 and defaults to 10000. Thus,
  trace sampling can be specified in 0.01% increments.
