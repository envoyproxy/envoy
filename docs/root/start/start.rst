.. _start:

Getting Started
===============

This section gets you started with a very simple configuration and provides some example configurations.

The fastest way to get started using Envoy is :ref:`installing pre-built binaries <install_binaries>`.
You can also :ref:`build it <building>` from source.

These examples use the :ref:`v3 Envoy API <envoy_api_reference>`, but use only the static configuration
feature of the API, which is most useful for simple requirements. For more complex requirements
:ref:`Dynamic Configuration <arch_overview_dynamic_config>` is supported.

.. toctree::
    :maxdepth: 2

    install
    docker
    quick-start
    install/ref_configs
    install/tools/tools
    building

Sandboxes
---------

We've created a number of sandboxes using Docker Compose that set up different
environments to test out Envoy's features and show sample configurations. As we
gauge peoples' interests we will add more sandboxes demonstrating different
features. The following sandboxes are available:

.. toctree::
    :maxdepth: 2

    sandboxes/cache
    sandboxes/cors
    sandboxes/csrf
    sandboxes/ext_authz
    sandboxes/fault_injection
    sandboxes/front_proxy
    sandboxes/grpc_bridge
    sandboxes/jaeger_native_tracing
    sandboxes/jaeger_tracing
    sandboxes/load_reporting_service
    sandboxes/lua
    sandboxes/mysql
    sandboxes/postgres
    sandboxes/redis
    sandboxes/wasm-cc
    sandboxes/zipkin_tracing
