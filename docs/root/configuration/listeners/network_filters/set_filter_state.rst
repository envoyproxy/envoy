.. _config_network_filters_set_filter_state:

Set-Filter-State Network Filter
===============================
* This filter should be configured with the type URL ``type.googleapis.com/envoy.extensions.filters.network.set_filter_state.v3.Config``.
* :ref:`v3 API reference <envoy_v3_api_msg_extensions.filters.network.set_filter_state.v3.Config>`

This filter is configured with a sequence of values to update the connection
filter state using the connection data. The filter state value can then be used
for routing, load balancing decisions, telemetry, etc. See :ref:`the well-known
filter state keys <well_known_filter_state>` for the controls used by Envoy
extensions.

.. warning::
    This filter allows overriding the behavior of other extensions and
    significantly and indirectly altering the connection processing logic.


Examples
--------

A sample filter configuration that propagates the downstream SNI as the upstream SNI:

.. validated-code-block:: yaml
  :type-name: envoy.extensions.filters.http.set_filter_state.v3.Config

  on_new_connection:
  - object_key: envoy.network.upstream_server_name
    format_string:
      text_format_source:
        inline_string: "%REQUESTED_SERVER_NAME%"


Statistics
----------

Currently, this filter generates no statistics.
