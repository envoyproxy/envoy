.. _config_wasm_runtime:

Wasm runtime
============

The following runtimes are supported by Envoy:

.. csv-table::
  :header: Name, Description
  :widths: 1, 2

  envoy.wasm.runtime.v8, "`V8 <https://v8.dev>`_-based runtime"
  envoy.wasm.runtime.wamr, "`WAMR <https://github.com/bytecodealliance/wasm-micro-runtime>`_ runtime"
  envoy.wasm.runtime.wasmtime, "`Wasmtime <https://github.com/bytecodealliance/wasmtime>`_ runtime"
  envoy.wasm.runtime.null, "Compiled modules linked into Envoy"

WAMR(WASM-Micro-Runtime), Wasmtime runtime is not included in Envoy release image by default.

Wasm runtime emits the following statistics:

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  wasm.<runtime>.created, Counter, Total number of execution instances created
  wasm.<runtime>.active, Gauge, Number of active execution instances
  wasm.wasm_vm_count, Gauge, "Process-wide number of active Wasm VMs, across every runtime"

.. _config_wasm_custom_metrics:

Custom metrics
--------------

By default, custom metrics are never removed. With :ref:`enable_eviction
<envoy_v3_api_field_extensions.wasm.v3.CustomMetrics.enable_eviction>` and a bootstrap
:ref:`stats_eviction_interval
<envoy_v3_api_field_config.bootstrap.v3.Bootstrap.stats_eviction_interval>`, custom metrics that
have not been updated during an eviction interval are removed:

* A counter or histogram that is updated after it was removed is re-created from zero. Consumers of
  cumulative values, such as Prometheus, see a counter reset.
* A gauge is removed only if, in addition, its value is zero. A gauge with a non-zero value is
  never removed; set it to zero to allow its removal.
* ``get_metric`` returns zero for a metric that was removed, and does not re-create it.
* Metric ids stay valid after their metric is removed.

When a :ref:`max_counters <envoy_v3_api_field_extensions.wasm.v3.CustomMetrics.max_counters>`,
:ref:`max_gauges <envoy_v3_api_field_extensions.wasm.v3.CustomMetrics.max_gauges>` or
:ref:`max_histograms <envoy_v3_api_field_extensions.wasm.v3.CustomMetrics.max_histograms>` limit is
reached, updates to metrics that cannot be created are discarded.

Eviction bounds the number of exported metrics. Envoy still keeps a small entry per worker thread
for every distinct metric name that a module has defined.
