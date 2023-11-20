.. _config_http_filters_set_filter_state:

Set-Filter-State HTTP Filter
============================
* This filter should be configured with the type URL ``type.googleapis.com/envoy.extensions.filters.http.set_filter_state.v3.Config``.
* :ref:`v3 API reference <envoy_v3_api_msg_extensions.filters.http.set_filter_state.v3.Config>`

This filter is configured with a sequence of values to update the request
filter state using the request data. The filter state value can then be used
for routing, load balancing decisions, telemetry, etc. See :ref:`the well-known
filter state keys <well_known_filter_state>` for the controls used by Envoy
extensions.

.. warning::
    This filter allows overriding the behavior of other extensions and
    significantly and indirectly altering the request processing logic.


Examples
--------

A sample filter configuration to capture the authority header from an HTTP
CONNECT request as the original destination cluster dynamic destination
address:

.. validated-code-block:: yaml
  :type-name: envoy.extensions.filters.http.set_filter_state.v3.Config

  on_request_headers:
  - object_key: envoy.network.transport_socket.original_dst_address
    format_string:
      text_format_source:
        inline_string: "%REQ(:AUTHORITY)%"


A sample filter configuration to capture the authority header and the remote
address from an HTTP CONNECT request as the original destination listener
filter addresses on the upstream :ref:`internal listener connection
<config_internal_listener>`:

.. validated-code-block:: yaml
  :type-name: envoy.extensions.filters.http.set_filter_state.v3.Config

  on_request_headers:
  - object_key: envoy.filters.listener.original_dst.local_ip
    format_string:
      text_format_source:
        inline_string: "%REQ(:AUTHORITY)%"
    shared_with_upstream: ONCE
  - object_key: envoy.filters.listener.original_dst.remote_ip
    format_string:
      text_format_source:
        inline_string: "%DOWNSTREAM_REMOTE_ADDRESS%"
    shared_with_upstream: ONCE


Statistics
----------

Currently, this filter generates no statistics.
