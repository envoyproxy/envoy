.. _config_wasm_runtime:

Wasm runtime
============

The following runtimes are supported by Envoy:

.. csv-table::
  :header: Name, Description
  :widths: 1, 2

  envoy.wasm.runtime.v8, "`V8<https://v8.dev>`-based runtime"
  envoy.wasm.runtime.wasmtime, "`Wasmtime<https://github.com/bytecodealliance/wasmtime>` runtime"
  envoy.wasm.runtime.wavm, "`WAVM<https://github.com/WAVM/WAVM>` runtime"
  envoy.wasm.runtime.null, "Compiled modules linked into Envoy"

Wasm runtime emits the following statistics:

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  wasm.<runtime>.created, Counter, Total number of execution instances created
  wasm.<runtime>.active, Gauge, Number of active execution instances
