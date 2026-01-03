.. _config_udp_listener_filters_dynamic_modules:

Dynamic Modules
===============

* :ref:`v3 API reference <envoy_v3_api_msg_extensions.filters.udp.dynamic_modules.v3.DynamicModuleUdpListenerFilter>`

The Dynamic Modules UDP listener filter allows you to write UDP listener filters in a dynamic module.
This can be used to implement custom UDP handling logic, such as:

*   Inspecting UDP datagrams.
*   Modifying UDP datagrams.
*   Dropping UDP datagrams.
*   Sending responses directly from the filter (e.g., for DNS).

The filter is configured using the :ref:`DynamicModuleUdpListenerFilter <envoy_v3_api_msg_extensions.filters.udp.dynamic_modules.v3.DynamicModuleUdpListenerFilter>` message.

.. code-block:: yaml

  listener_filters:
  - name: envoy.filters.udp_listener.dynamic_modules
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.udp.dynamic_modules.v3.DynamicModuleUdpListenerFilter
      dynamic_module_config:
        name: my_module
        entry_point: envoy_dynamic_module_on_program_init
      filter_name: my_udp_filter
      filter_config:
        "@type": type.googleapis.com/google.protobuf.StringValue
        value: "my_config"

For more details on dynamic modules, see the :ref:`architecture overview <arch_overview_dynamic_modules>`.
