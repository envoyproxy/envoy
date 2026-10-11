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
  wasm.<runtime>.memory_size, Gauge, "Total linear memory size in bytes of all active execution instances. Wasm linear memory never shrinks, so an instance's share only drops when it is destroyed. Not reported for the ``null`` runtime"
  wasm.wasm_vm_count, Gauge, "Process-wide number of active Wasm VMs, across every runtime"

Each Wasm plugin also emits the following statistics, where ``<name>`` is the plugin
:ref:`name <envoy_v3_api_field_extensions.wasm.v3.PluginConfig.name>`:

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  wasm.<name>.vm_reload_backoff, Counter, "Number of reloads of a failed execution instance skipped because of the reload backoff, with the ``FAIL_RELOAD`` failure policy"
  wasm.<name>.vm_reload_success, Counter, "Number of successful reloads of a failed execution instance, with the ``FAIL_RELOAD`` failure policy"
  wasm.<name>.vm_reload_failure, Counter, "Number of failed reloads of a failed execution instance, with the ``FAIL_RELOAD`` failure policy"
  wasm.<name>.vm_memory_size, Gauge, "Sum of the linear memory size in bytes of the per-worker execution instances serving the plugin. Not reported for the ``null`` runtime"

Each worker thread runs its own execution instance of a plugin, with its own linear memory, so the
instances of a plugin on different workers can have different sizes. ``wasm.<name>.vm_memory_size``
adds up the instances of all workers, each sampled the last time the plugin was used on that worker
(e.g. for a request). Plugins with the same
:ref:`vm_config <envoy_v3_api_field_extensions.wasm.v3.PluginConfig.vm_config>` share their
instances, so each of them reports the full size of the shared instances, and the sum over plugins
can exceed ``wasm.<runtime>.memory_size``. ``wasm.<runtime>.memory_size`` counts every instance
once, including the ones that serve no requests, and is the total memory used by the runtime.
