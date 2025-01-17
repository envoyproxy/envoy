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
