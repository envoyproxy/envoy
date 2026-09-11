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
  information. Defaults to 100.

.. _config_http_conn_man_runtime_path_with_escaped_slashes_action:

http_connection_manager.path_with_escaped_slashes_action
  Overrides Envoy's default action taken when the
  :ref:`path_with_escaped_slashes_action <envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.path_with_escaped_slashes_action>`.
  was not specified or set to the IMPLEMENTATION_SPECIFIC_DEFAULT value. Possible values:

  - 2 sets action to the REJECT_REQUEST.
  - 3 sets action to the UNESCAPE_AND_REDIRECT.
  - 4 sets action to the UNESCAPE_AND_FORWARD.
  - all other values set the action to KEEP_UNCHANGED.

.. _config_http_conn_man_runtime_path_with_escaped_slashes_action_enabled:

http_connection_manager.path_with_escaped_slashes_action_enabled
  % of requests that will be subject to the
  :ref:`path_with_escaped_slashes_action <envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.path_with_escaped_slashes_action>`.
  action. For all other requests the KEEP_UNCHANGED action will be applied. Defaults to 100.
