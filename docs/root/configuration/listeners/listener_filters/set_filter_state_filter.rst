.. _config_listener_filters_set_filter_state:

Set-Filter-State Listener Filter
================================
* This filter should be configured with the type URL ``type.googleapis.com/envoy.extensions.filters.listener.set_filter_state.v3.Config``.
* :ref:`v3 API reference <envoy_v3_api_msg_extensions.filters.listener.set_filter_state.v3.Config>`

This filter is configured with a sequence of values to update the connection
filter state using the connection metadata prior to network filter chains. The
filter state value can then be used for routing, load balancing decisions,
telemetry, etc. See :ref:`the well-known filter state keys
<well_known_filter_state>` for the controls used by Envoy extensions.

The filter applies values at the following point in the connection lifecycle:

* ``on_accept``: applied when a new downstream socket is accepted.

.. warning::
    This filter allows overriding the behavior of other extensions and
    significantly and indirectly altering the connection processing logic.


Examples
--------

A sample filter configuration using a **custom key** with the generic string factory.
Use this pattern when you want to store arbitrary connection data under a custom name:

.. validated-code-block:: yaml
  :type-name: envoy.extensions.filters.listener.set_filter_state.v3.Config

  on_accept:
  - object_key: my.custom.client_address
    factory_key: envoy.string
    format_string:
      text_format_source:
        inline_string: "%DOWNSTREAM_REMOTE_ADDRESS%"

The stored value can then be accessed in access logs using ``%FILTER_STATE(my.custom.client_address)%``.


Statistics
----------

Currently, this filter generates no statistics.
