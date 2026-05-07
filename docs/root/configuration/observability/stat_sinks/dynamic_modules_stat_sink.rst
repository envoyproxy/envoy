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
   development; capabilities and ABI are expected to evolve.

How it works
------------

1. At server startup Envoy loads the shared object via ``dlopen`` using the
   :ref:`dynamic_module_config <envoy_v3_api_field_extensions.stat_sinks.dynamic_modules.v3.DynamicModuleStatsSink.dynamic_module_config>`
   specification (either name-based search through
   ``ENVOY_DYNAMIC_MODULES_SEARCH_PATH`` or an explicit
   ``module.local.filename``).
2. Envoy resolves four event hooks the module must export:

   * ``envoy_dynamic_module_on_stat_sink_config_new``
   * ``envoy_dynamic_module_on_stat_sink_config_destroy``
   * ``envoy_dynamic_module_on_stat_sink_flush``
   * ``envoy_dynamic_module_on_stat_sink_on_histogram_complete``

3. The module receives the configured
   :ref:`sink_name <envoy_v3_api_field_extensions.stat_sinks.dynamic_modules.v3.DynamicModuleStatsSink.sink_name>`
   and
   :ref:`sink_config <envoy_v3_api_field_extensions.stat_sinks.dynamic_modules.v3.DynamicModuleStatsSink.sink_config>`
   payload in ``on_stat_sink_config_new`` and returns an opaque handle that
   Envoy passes back on every subsequent callback.
4. Every ``stats_flush_interval`` Envoy invokes ``on_stat_sink_flush`` on the
   stats flush thread. The module iterates counters, gauges, and text readouts
   through snapshot-reader callbacks Envoy provides.
5. Every histogram observation synchronously calls
   ``on_stat_sink_on_histogram_complete`` on whichever worker thread recorded
   the sample. Implementations must be thread-safe and fast; buffering samples
   and draining them from a worker-owned thread is recommended.

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

Configuration payloads
----------------------

The :ref:`sink_config <envoy_v3_api_field_extensions.stat_sinks.dynamic_modules.v3.DynamicModuleStatsSink.sink_config>`
field is a ``google.protobuf.Any``; Envoy serializes the contents before
handing them to the module:

* ``google.protobuf.StringValue`` / ``google.protobuf.BytesValue`` -- passed
  through as raw bytes.
* ``google.protobuf.Struct`` -- serialized to JSON before delivery.
* Any other message type -- raw proto bytes.

Go-module caveats
-----------------

Modules compiled from Go should generally set
:ref:`do_not_close <envoy_v3_api_field_extensions.dynamic_modules.v3.DynamicModuleConfig.do_not_close>`
to ``true`` (the Go runtime cannot survive ``dlclose``) and often need
:ref:`load_globally <envoy_v3_api_field_extensions.dynamic_modules.v3.DynamicModuleConfig.load_globally>`
set to ``true`` when multiple Go-built shared objects coexist in the same
process.
