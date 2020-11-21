.. _arch_overview_threading:

Threading model
===============

Envoy uses a single process with multiple threads architecture. A single *primary* thread controls
various sporadic coordination tasks while some number of *worker* threads perform listening,
filtering, and forwarding. Once a connection is accepted by a listener, the connection spends the
rest of its lifetime bound to a single worker thread. This allows the majority of Envoy to be
largely single threaded (embarrassingly parallel) with a small amount of more complex code handling
coordination between the worker threads. Generally Envoy is written to be 100% non-blocking and for
most workloads we recommend configuring the number of worker threads to be equal to the number of
hardware threads on the machine.

Listener connection balancing
-----------------------------

By default, there is no coordination between worker threads. This means that all worker threads
independently attempt to accept connections on each listener and rely on the kernel to perform
adequate balancing between threads. For most workloads, the kernel does a very good job of
balancing incoming connections. However, for some workloads, particularly those that have a small
number of very long lived connections (e.g., service mesh HTTP2/gRPC egress), it may be desirable
to have Envoy forcibly balance connections between worker threads. To support this behavior,
Envoy allows for different types of :ref:`connection balancing
<envoy_v3_api_field_config.listener.v3.Listener.connection_balance_config>` to be configured on each :ref:`listener
<arch_overview_listeners>`.
