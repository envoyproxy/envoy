.. _arch_overview_wasm:

Wasm
====

Envoy supports execution of the Wasm modules implemented against the `Proxy-Wasm specification <https://github.com/proxy-wasm/spec>`_.
Currently, the `binary interface version 0.2.1 <https://github.com/proxy-wasm/spec/tree/main/abi-versions/v0.2.1>`_ is recommended.
Wasm offers a portable and compact binary executable format that can be compiled once from a variety of languages
(`C++ <https://github.com/proxy-wasm/proxy-wasm-cpp-sdk>`_, `Rust <https://github.com/proxy-wasm/proxy-wasm-rust-sdk>`_ and
run anywhere. Please see the :ref:`sandbox example <install_sandboxes_wasm_filter>` for instructions on how to create and deploy
a Wasm module.

Execution model for filters
---------------------------

At runtime, the Wasm modules are executed inline in the worker threads via a series of stream callbacks as defined by
the ABI. The workers operate independently and do not share the Wasm execution instances and their memory. Asychronous
operations, such as sub-requests, are accomplished via host calls and subsequent guess callbacks since there is no
concurrency in the Wasm runtime.

At configuration time, the Wasm modules are loaded into the Wasm engine on the main thread. A separate Wasm execution
instance is spawned for each combination of the module binary and the :ref:`vm_id
<envoy_v3_api_field_extensions.wasm.v3.VmConfig.vm_id>` field value. The instance receives the Wasm configuration via a
callback, possibly repeatedly for each xDS listener encapsulating the Wasm filter. If the callback accepts the
configuration, the main execution instance is cloned to each worker thread.

Note that this configuration model is distinct for the Wasm filter. Unlike a regular HTTP filter that is instantiated
independently for each xDS listener, xDS listeners share the same main Wasm execution instance across xDS updates.

Envoy Attributes
----------------

Wasm ABI exposes Envoy-specific host attributes via the dedicated `proxy_get_property
<https://github.com/proxy-wasm/spec/tree/main/abi-versions/v0.2.1#proxy_get_property>`_ interface stub. These are the
standard :ref:`attributes <arch_overview_attributes>` and the values are returned via the type-specific binary
serialization.

Foreign functions
-----------------

Envoy offers additional functionality over the Proxy-Wasm ABI via `proxy_call_foreign_function
<https://github.com/proxy-wasm/spec/tree/main/abi-versions/v0.2.1#proxy_call_foreign_function>`_ binary interface:

* ``verify_signature`` verifies cryptographic signatures.
* ``compress`` applies ``zlib`` compression.
* ``uncompress`` applies ``zlib`` decompression.
* ``declare_property`` creates a placeholder filter state object with the type information.
* ``set_envoy_filter_state`` set a filter state object.
* ``clear_route_cache`` updates the selected route.
* ``expr_create`` compiles a CEL expression for evaluation.
* ``expr_evalute`` evaluates a compiled CEL expression.
* ``expr_delete`` deletes a compiled CEL expression.
