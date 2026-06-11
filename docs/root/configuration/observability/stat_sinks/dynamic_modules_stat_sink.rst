.. _config_stat_sinks_dynamic_modules:

Dynamic Modules Stats Sink
==========================

The :ref:`DynamicModuleStatsSink <envoy_v3_api_msg_extensions.stat_sinks.dynamic_modules.v3.DynamicModuleStatsSink>`
configuration specifies a stats sink backed by a :ref:`dynamic module <arch_overview_dynamic_modules>`:
a shared object file loaded via ``dlopen`` that implements a small set of C ABI
callbacks. Envoy hands the module each periodic metric snapshot during flush
and every histogram observation synchronously, letting a module written in any
language that can produce an ELF (C, Rust, Go, etc.) ship metrics to any
backend without requiring a custom Envoy build.

* This extension should be configured with the type URL
  ``type.googleapis.com/envoy.extensions.stat_sinks.dynamic_modules.v3.DynamicModuleStatsSink``.
* :ref:`v3 API reference <envoy_v3_api_msg_extensions.stat_sinks.dynamic_modules.v3.DynamicModuleStatsSink>`

.. attention::

   Dynamic modules run in-process with the same privileges as Envoy. Only
   load modules you trust. This extension is currently under active
   development. Capabilities and ABI are expected to evolve.

Configuration example
---------------------

.. code-block:: yaml

  stats_flush_interval: 10s

  stats_sinks:
    - name: envoy.stat_sinks.dynamic_modules
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.stat_sinks.dynamic_modules.v3.DynamicModuleStatsSink
        dynamic_module_config:
          name: my_stats_sink
          do_not_close: true
        sink_name: my_sink_impl
        sink_config:
          "@type": type.googleapis.com/google.protobuf.Struct
          value:
            endpoint: "metrics.example.com:9125"
            prefix: "envoy."
