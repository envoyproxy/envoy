.. _arch_overview_dynamic_modules:

Dynamic modules
===============

.. attention::

   The dynamic modules feature is currently under active development.
   Capabilities will be expanded over time and it still lacks some features that are available in other extension mechanisms.
   We are looking for feedback from the community to improve the feature.

Envoy has support for loading shared libraries at runtime to extend its functionality. In Envoy, these are known as "dynamic modules." More specifically, dynamic modules are shared libraries that implement the
:repo:`ABI <source/extensions/dynamic_modules/abi.h>` written in a pure C header file. The ABI defines a set of functions
that the dynamic module must implement to be loaded by Envoy. Also, it specifies the functions implemented by Envoy
that the dynamic module can call to interact with Envoy.

Implementing the ABI from scratch requires an extensive understanding of the Envoy internals. For users, we provide an
official SDK that abstracts these details and provides a high-level API to implement dynamic modules. The SDK is currently
available in Rust. In theory, any language that can produce a shared library can be used to implement dynamic modules.
Future development may include support for other languages.

Currently, dynamic modules are supported at the following extension points:

* As an :ref:`HTTP filter <envoy_v3_api_msg_extensions.filters.http.dynamic_modules.v3.DynamicModuleFilter>`.
* As a :ref:`network filter <envoy_v3_api_msg_extensions.filters.network.dynamic_modules.v3.DynamicModuleNetworkFilter>`.

There are a few design goals for the dynamic modules:

1. **Performance**: The dynamic modules should have minimal overhead compared to the built-in C++ extensions. For example, the dynamic modules are able to access HTTP headers as well as body without copying them unlike any other extension mechanisms.
2. **Ease of Use**: The SDK should provide a high-level API that abstracts the details of the Envoy internals.
3. **Flexibility**: The dynamic modules should be able to implement any functionality that can be implemented by the built-in C++ extensions without performance penalty. This is work in progress and many features are not yet available.

Compatibility
--------------------------

Since a dynamic modules is loaded at runtime, it must be abi-compatible with the
Envoy binary that loads it.

Envoy's dynamic modules have stricter compatibility requirements than Envoy's other extension mechanisms, such as Lua, Wasm or External Processor.
Stabilizing the ABI is challenging due to the way the ABI needs to be tightly coupled to Envoy's internals. Even though
our ultimate goal is to have a stable ABI that can be used across different versions of Envoy, we currently do not guarantee any compatibility
between different versions.

In other words, the dynamic modules must be built with the SDK of the same version as the Envoy binary that loads the dynamic module.
Since the SDK lives inside the Envoy repository, using the same commit hash or release tag of the Envoy version is the best way to ensure
the compatibility.

Module discovery
--------------------------

A dynamic module is referenced by its name as in the :ref:`configuration API  <envoy_v3_api_msg_extensions.dynamic_modules.v3.DynamicModuleConfig>`.
The name is used to search for the shared library file in the search path. The search path is configured by the environment variable
``ENVOY_DYNAMIC_MODULES_SEARCH_PATH``. The actual search path is ``${ENVOY_DYNAMIC_MODULES_SEARCH_PATH}/lib${name}.so``.

For example, when the name ``my_module`` is referenced in the configuration and the search path is set to ``/path/to/modules``, Envoy will look for
``/path/to/modules/libmy_module.so``.

Safety
--------------------------
The dynamic modules should be used under the assumption that all modules are fully trusted and have the same privilege level as the main Envoy program.
Since these modules run in the same process as Envoy, they can access all memory and resources available to the main process.
This makes it unfeasible to enforce security boundaries between Envoy and the modules, as they share the same address space and permissions.
It is essential that any dynamic module undergo thorough testing and validation before deployment just like any other application code.

Getting started
--------------------------

We have a dedicated repository for the dynamic module examples to help you get started.
The repository is available at `envoyproxy/dynamic-modules-examples <https://github.com/envoyproxy/dynamic-modules-examples>`_
