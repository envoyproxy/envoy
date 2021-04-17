.. _faq_resource_limits:

Envoy 如何防止文件描述符耗尽？
================================

:ref:`每个监听器的连接限制 <config_listeners_runtime>` 可以配置为特定侦听器将接受的活动连接数的上限。在工作线程数量的顺序上，侦听器可以接受的连接数量多于配置值。

另外，可以配置 :ref:`全局限制 <config_overload_manager>` 应用于所有监听器连接数上。

在基于 Unix 的系统上，建议将所有连接限制的总和保持小于系统文件描述符限制的一半，以考虑上游连接、文件和文件描述符的其他使用。

.. note::

    这个监听器连接的限制最终将由 :ref:`过载管理器 <arch_overview_overload_manager>` 来处理。
