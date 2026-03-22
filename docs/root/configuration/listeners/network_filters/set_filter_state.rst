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

The filter can apply values at different points in the connection lifecycle:

* ``on_new_connection``: applied when a new downstream connection is accepted.
* ``on_downstream_tls_handshake``: applied when the downstream TLS handshake is complete. For
  non-TLS downstream connections (where there is no TLS handshake), this list is applied when the
  new connection is accepted.

.. warning::
    This filter allows overriding the behavior of other extensions and
    significantly and indirectly altering the connection processing logic.


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

A sample filter configuration that propagates the downstream SNI as the upstream SNI:

.. validated-code-block:: yaml
  :type-name: envoy.extensions.filters.network.set_filter_state.v3.Config

  on_new_connection:
  - object_key: envoy.network.upstream_server_name
    format_string:
      text_format_source:
        inline_string: "%REQUESTED_SERVER_NAME%"


A sample filter configuration using a **custom key** with the generic string factory.
Use this pattern when you want to store arbitrary connection data under a custom name:

.. validated-code-block:: yaml
  :type-name: envoy.extensions.filters.network.set_filter_state.v3.Config

  on_new_connection:
  - object_key: my.custom.client_sni
    factory_key: envoy.string
    format_string:
      text_format_source:
        inline_string: "%REQUESTED_SERVER_NAME%"

The stored value can then be accessed in access logs using ``%FILTER_STATE(my.custom.client_sni)%``.

When you need to populate filter state using information that is only available after the downstream
TLS handshake completes (e.g., downstream peer certificate SANs), use
``on_downstream_tls_handshake``:

.. validated-code-block:: yaml
  :type-name: envoy.extensions.filters.network.set_filter_state.v3.Config

  on_downstream_tls_handshake:
  - object_key: my.custom.downstream_peer_uri_san
    factory_key: envoy.string
    format_string:
      text_format_source:
        inline_string: "%DOWNSTREAM_PEER_URI_SAN%"


Statistics
----------

Currently, this filter generates no statistics.
