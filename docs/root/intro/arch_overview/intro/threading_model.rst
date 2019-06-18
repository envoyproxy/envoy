.. _arch_overview_threading:

Threading model
===============

Envoy uses a single process with multiple threads architecture. A single *master* thread controls
various sporadic coordination tasks while some number of *worker* threads perform listening,
filtering, and forwarding. Once a connection is accepted by a listener, the connection spends the
rest of its lifetime bound to a single worker thread. This allows the majority of Envoy to be
largely single threaded (embarrassingly parallel) with a small amount of more complex code handling
coordination between the worker threads. Generally Envoy is written to be 100% non-blocking and for
most workloads we recommend configuring the number of worker threads to be equal to the number of
hardware threads on the machine.
