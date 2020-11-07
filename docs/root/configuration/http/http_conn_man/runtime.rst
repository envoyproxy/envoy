.. _config_http_conn_man_runtime:

Runtime
=======

The HTTP connection manager supports the following runtime settings:

.. _config_http_conn_man_runtime_normalize_path:

http_connection_manager.normalize_path
  % of requests that will have path normalization applied if not already configured in
  :ref:`normalize_path <envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.normalize_path>`.
  This is evaluated at configuration load time and will apply to all requests for a given
  configuration.

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
