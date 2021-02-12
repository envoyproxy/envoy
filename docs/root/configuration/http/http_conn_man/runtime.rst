.. _config_http_conn_man_runtime:

Runtime
=======

The HTTP connection manager supports the following runtime settings:

.. _config_http_conn_man_runtime_normalize_path:

http_connection_manager.normalize_path % of requests that will have path normalization applied if
  not already configured in :ref:`normalize_path
  <envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.normalize_path>`
  or :ref:`path_normalization_options
  <envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.path_normalization_options>`.
  This is evaluated at configuration load time and will apply to all requests for a given
  configuration. This will apply to the forwarded *:path* header unless :ref:`policy
  <envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.path_normalization_options.policy` or runtime feature `http_connection_manager.forward_normalized_path`
  specifies otherwise.

.. _config_http_conn_man_runtime_forward_normalized_path:

http_connection_manager.forward_normalized_path
  normalization will apply to the forwarded *:path* header in addition to internally for
  matching and routing. If false, the original path will be forwarded upstream, unless a filter
  has changed the *:path* header during processing. Defaults to true.

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
