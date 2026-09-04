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


Understanding Object and Factory Keys
-------------------------------------

The filter state system uses a factory pattern to create objects from string values.
Each filter state entry consists of:

* **object_key**: The name under which the data is stored and retrieved.
* **factory_key**: The name of the factory that creates the object from the string value.

When using :ref:`well-known filter state keys <well_known_filter_state>` (like
``envoy.tcp_proxy.cluster`` or ``envoy.network.upstream_server_name``), each key has a
factory registered with the same name. In this case, you only need to specify ``object_key``
and the system will automatically use a factory with the same name.

When using a **custom key name** which is not from the well-known list, no factory is registered
with that name. You must specify ``factory_key`` to tell the system which factory should
create the object. Use ``envoy.string`` as the factory for generic string values.


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


A sample filter configuration using a **custom key** with the generic string factory.
Use this pattern when you want to store arbitrary data under a custom name for use
in access logging, Lua scripts, or other custom processing:

.. validated-code-block:: yaml
  :type-name: envoy.extensions.filters.http.set_filter_state.v3.Config

  on_request_headers:
  - object_key: my.custom.request_id
    factory_key: envoy.string
    format_string:
      text_format_source:
        inline_string: "%REQ(x-request-id)%"

The stored value can then be accessed in access logs using ``%FILTER_STATE(my.custom.request_id)%``.


Statistics
----------

Currently, this filter generates no statistics.
