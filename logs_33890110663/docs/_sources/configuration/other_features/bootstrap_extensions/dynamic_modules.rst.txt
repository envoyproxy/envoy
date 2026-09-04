.. _config_bootstrap_extensions_dynamic_modules:

Dynamic Modules
===============

* :ref:`v3 API reference <envoy_v3_api_msg_extensions.bootstrap.dynamic_modules.v3.DynamicModuleBootstrapExtension>`

The Dynamic Modules bootstrap extension allows you to write bootstrap extensions in a dynamic module.
This can be used to implement:

* Server initialization logic when Envoy starts.
* Per-worker-thread initialization logic when worker threads start.
* Singleton patterns for configuration loading from external services.
* Global state management across filters.
* Background tasks that run on the main thread.

The extension is configured using the :ref:`DynamicModuleBootstrapExtension <envoy_v3_api_msg_extensions.bootstrap.dynamic_modules.v3.DynamicModuleBootstrapExtension>` message.

Example Configuration
---------------------

.. code-block:: yaml

  bootstrap_extensions:
  - name: envoy.bootstrap.dynamic_modules
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.bootstrap.dynamic_modules.v3.DynamicModuleBootstrapExtension
      dynamic_module_config:
        name: my_module
      extension_name: my_bootstrap_extension
      extension_config:
        "@type": type.googleapis.com/google.protobuf.StringValue
        value: "my_config"

Lifecycle Hooks
---------------

Bootstrap extensions have two lifecycle hooks:

**onServerInitialized**
  Called when the server is fully initialized, on the main thread. This is where you can:

  * Load configuration from external services.
  * Initialize global state.
  * Register singleton resources.
  * Start background tasks.

**onWorkerThreadInitialized**
  Called once per worker thread when it starts. This is where you can:

  * Initialize per-worker-thread state.
  * Set up thread-local storage.
  * Prepare resources for use by filters on this thread.

Use Cases
---------

**Dynamic Configuration Loading**
  Use the ``onServerInitialized`` hook to fetch configuration from an external service (e.g., a config server)
  at startup. Store the configuration in shared state that filters can access.

**Singleton Pattern**
  Implement a singleton that is initialized once and shared across all filters. For example, a connection pool
  to an external service, or a cache that is shared across all requests.

**Global Metrics**
  Initialize custom metrics in the ``onServerInitialized`` hook that can be updated by filters throughout
  the request lifecycle.

For more details on dynamic modules, see the :ref:`architecture overview <arch_overview_dynamic_modules>`.
