.. _arch_overview_wasm:

Wasm
====

Envoy supports execution of Wasm plugins: the Wasm modules written against the `Proxy-Wasm specification
<https://github.com/proxy-wasm/spec>`_, which defines an extension interface through which the modules can implement
custom extension logic. Currently, the `Proxy-Wasm ABI (Application Binary Interface) version 0.2.1
<https://github.com/proxy-wasm/spec/tree/main/abi-versions/v0.2.1>`_ is recommended. Wasm offers a portable and compact
binary executable format that can be compiled once from a variety of languages (`C++
<https://github.com/proxy-wasm/proxy-wasm-cpp-sdk>`_, `Rust <https://github.com/proxy-wasm/proxy-wasm-rust-sdk>`_) and
run anywhere. Please see the :ref:`sandbox example <install_sandboxes_wasm_filter>` for instructions on how to create
and deploy a Wasm module.

Envoy provides several extension points at which Wasm plugins can be invoked:

* As an :ref:`HTTP filter <envoy.extensions.filters.http.wasm.v3.Wasm>`
* As a :ref:`network-level (L4) filter <envoy.extensions.filters.network.wasm.v3.Wasm>`
* As a :ref:`StatsSink <envoy.extensions.stat_sinks.wasm.v3.Wasm>`
* As an :ref:`AccessLogger <envoy.extensions.access_loggers.wasm.v3.WasmAccessLog>`
* As a :ref:`background service <envoy.extensions.wasm.v3.WasmService>`

The particular functions that Envoy invokes on a Wasm plugin depend on the extension point at which it is configured.

Contexts, plugins, and VMs
--------------------------

A Wasm context refers to the interface implementation between the host (Envoy) and the guest (Wasm bytecode). For an
HTTP filter, there are two types of contexts: a *root context* (context with ID 0) represents the configuration-only
context, and a *per-request context* that is created once per each request.

A Wasm VM refers to the execution instance of a Wasm bytecode module. Multiple VMs can be created for the same bytecode
when using the ``vm_id`` field, and they each would have separate per-VM memory. The opposite is also true, the same VM
can contain multiple implementations for different extensions (plugins), to be used in different contexts. VM sharing
optimizes the runtime overhead and reduces the total binary size of the bytecode since the extensions can share the
code.

A Wasm plugin is the link between the context and the VM. It identifies which Wasm VM to use for a context, which
extension to invoke within the Wasm VM execution instance (via the :ref:`root_id
<envoy_v3_api_field_extensions.wasm.v3.PluginConfig.root_id>` field value), and how to route the configuration call to
the extension during the xDS update.


Execution model for filters
---------------------------

At runtime, the Wasm modules are executed inline in the worker threads via a series of stream callbacks as defined by
the ABI. The worker threads operate independently and do not share the Wasm execution instances and their runtime
memory. Asynchronous or blocking operations are delegated to Envoy to perform; upon completion, Envoy then invokes a
separate completion callback on the plugin.

At configuration time, the Wasm modules are loaded into the Wasm engine on the main thread. A separate Wasm execution
instance is spawned for each combination of the module binary and the :ref:`vm_id
<envoy_v3_api_field_extensions.wasm.v3.VmConfig.vm_id>` field value. The instance receives the Wasm configuration for
each plugin via a callback, possibly repeatedly for each xDS listener encapsulating the Wasm filter. If the callback
accepts the configuration, the main execution instance is cloned to each worker thread.

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

Wasm runtime
------------

Envoy Wasm can be :ref:`configured <envoy.extensions.wasm.v3.VmConfig.runtime>` to use one of several Wasm runtime
implementations: ``V8``, ``WAMR``, or ``Wasmtime``, as long as the runtime is included in the Envoy distribution.  There
is also a special pseudo-Wasm runtime, called the "Null VM", in which Wasm plugin code is compiled to native (non-Wasm)
code and statically linked directly into the Envoy binary.

