为什么轮询负载均衡看起来不均衡？
================================================

Envoy 采用的是孤立的 :ref:`线程模型 <arch_overview_threading>`。这意味着工作线程和在其上运行的负载平衡器不会相互协作。当使用诸如 :ref:`轮询 <arch_overview_load_balancing_types_round_robin>` 的负载均衡策略，同时使用多个工作线程时，可能会出现负载均衡无法正常工作的情况。如果需要，可以通过
:option:`--concurrency` 选项，去调整工作线程的数量。

孤立的执行模型也是每个上游都可以建立多个 HTTP/2 连接的原因；工作线程之间不共享 :ref:`连接池 <arch_overview_conn_pool>`。
