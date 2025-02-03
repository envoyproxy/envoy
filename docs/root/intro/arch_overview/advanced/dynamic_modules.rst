.. _arch_overview_dynamic_modules:

Dynamic Modules
===============

.. attention::

   The dynamic modules feature is an experimental and is currently under active development.
   Capabilities will be expanded over time and it still lacks some features that are available in other extension mechanisms.
   We are looking for feedback from the community to improve the feature.

Envoy has support for loading shared libraries at runtime to extend its functionality. We call the shared libraries that
are loadable by Envoy as "dynamic modules." More specifically, dynamic modules are shared libraries that implements the
:repo:`ABI <source/extensions/dynamic_modules/abi.h>` written in a pure C header file. The ABI defines a set of functions
that the dynamic module must implement to be loaded by Envoy. Also, it specifies the functions implemented by Envoy
that the dynamic module can call to interact with Envoy.

Implementing the ABI from scratch requires an extensive understanding of the Envoy internals. For users, we provide an
official SDK that abstracts these details and provides a high-level API to implement dynamic modules. The SDK is currently
available in Rust. In theory, any language that can produce a shared library can be used to implement dynamic modules.
Future development may include support for other languages.

Currently, the dynamic modules are only supported at the following extension points:

* As an * :ref:`HTTP filter  <envoy_v3_api_msg_extensions.filters.http.dynamic_modules.v3.DynamicModuleFilter>`


There are a few design goals for the dynamic modules:

1. **Performance**: The dynamic modules should have minimal overhead compared to the built-in C++ extensions. For example, the dynamic modules are able to access HTTP headers as well as body without copying them unlike any other extension mechanisms.
2. **Ease of Use**: The SDK should provide a high-level API that abstracts the details of the Envoy internals.
3. **Flexibility**: The dynamic modules should be able to implement any functionality that can be implemented by the built-in C++ extensions without performance penalty. This is work in progress and many features are not yet available.

Compatibility
--------------------------

Since the dynamic modules are loaded at runtime, it is important to ensure that the dynamic module is compatible with the
Envoy binary that loads it.

The dynamic modules have stricker compatibility requirements than the other Envoy extension mechanisms, such as Lua, Wasm or External Processor.
This is partly because the ABI is tightly coupled with the Envoy internals which requires a bit difficult to make a stable ABI. Even though
the ultimate goal is to make the ABI stable that can be used across different versions of Envoy, we currently do not guarantee any compatibility
between different versions of Envoy.

In other words, the dynamic modules must be built with the SDK of the same version as the Envoy binary that loads the dynamic module.
Since the SDK lives inside the Envoy repository, using the same commit hash or release tag of the Envoy version is the best way to ensure
the compatibility.


Module Discovery
--------------------------

A dynamic module is referenced by its name as in the :ref:`configuration API  <envoy_v3_api_msg_extensions.dynamic_modules.v3.DynamicModuleConfig>`.
The name is used to search for the shared library file in the search path. The search path is configured by the environment variable
``ENVOY_DYNAMIC_MODULES_SEARCH_PATH``. The actual search path is ``${ENVOY_DYNAMIC_MODULES_SEARCH_PATH}/lib${name}.so``.

For example, when the name ``my_module`` is referenced in the configuration. and the search path is set to ``/path/to/modules``, Envoy will look for
``/path/to/modules/libmy_module.so``.

Safety
--------------------------
The dynamic modules operate under the assumption that all modules are fully trusted and have the same privilege level as the main Envoy program.
Since these modules run in the same process as Envoy, they can access all memory and resources available to the main process.
This makes it unfeasible to enforce security boundaries between Envoy and the modules, as they share the same address space and permissions.

It is essential that any dynamic module undergo thorough testing and validation before deployment just like any other application code.


Getting Started
--------------------------

We have a dedicated repository for the dynamic module examples to help you get started.
The repository is available at `envoyproxy/dynamic-modules-example <https://github.com/envoyproxy/envoyproxy/dynamic-modules-example>`_
