.. _faq_resource_limits:

How does Envoy prevent file descriptor exhaustion?
==================================================

:ref:`Per-listener connection limits <config_listeners_runtime>` may be configured as an upper bound
on the number of active connections a particular listener will accept. The listener may accept more
connections than the configured value on the order of the number of worker threads.

In addition, one may configure a :ref:`global limit <config_overload_manager>` on the number of
connections that will apply across all listeners.

On Unix-based systems, it is recommended to keep the sum of all connection limits less than half of
the system's file descriptor limit to account for upstream connections, files, and other usage of
file descriptors.

.. note::

    This per-listener connection limiting will eventually be handled by the :ref:`overload manager
    <arch_overview_overload_manager>`.
