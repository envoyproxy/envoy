.. _config_listener_stats:

统计
==========

监听器
--------


每个监听器都有一个以 *listener.<address>.* 为根的统计树，其中包含以下统计信息：

.. csv-table::
   :header: 名称, 类型, 描述
   :widths: 1, 1, 2

   downstream_cx_total, Counter, 连接总数
   downstream_cx_destroy, Counter, 已销毁的连接总数
   downstream_cx_active, Gauge, 活跃的连接总数
   downstream_cx_length_ms, Histogram, 连接时长 (毫秒)
   downstream_cx_overflow, Counter, 由于强制执行监听器连接限制而拒绝的连接总数
   downstream_pre_cx_timeout, Counter, 在监听器过滤器处理过程中套接字的超时时间
   downstream_pre_cx_active, Gauge, 当前正在接受监听器过滤器处理的套接字
   global_cx_overflow, Counter, 由于强制全局连接限制而拒绝的连接总数
   no_filter_chain_match, Counter, 与任何过滤器链都不匹配的连接总数
   ssl.connection_error, Counter, TLS 连接错误总数，不包括证书认证失败
   ssl.handshake, Counter, TLS 连接握手成功总数
   ssl.session_reused, Counter, TLS 会话恢复成功总数
   ssl.no_certificate, Counter, 不带客户端证书的 TLS 连接成功总数
   ssl.fail_verify_no_cert, Counter, 因为缺少客户端证书而失败的 TLS 连接总数
   ssl.fail_verify_error, Counter, CA 认证失败的 TLS 连接总数
   ssl.fail_verify_san, Counter, SAN 认证失败的 TLS 连接总数
   ssl.fail_verify_cert_hash, Counter, 认证 pinning 认证失败的 TLS 连接总数
   ssl.ocsp_staple_failed, Counter, 未能符合 OCSP (Online Certificate Status Protocol) 策略的 TLS 连接总数
   ssl.ocsp_staple_omitted, Counter, 未装订 OCSP 响应而成功完成的 TLS 连接总数
   ssl.ocsp_staple_responses, Counter, 有效 OCSP 响应可用的 TLS 连接总数（无论客户端是否请求装订）
   ssl.ocsp_staple_requests, Counter, 客户端请求 OCSP 装订的 TLS 连接总数
   ssl.ciphers.<cipher>, Counter, 使用密码 <cipher> 的成功 TLS 连接总数
   ssl.curves.<curve>, Counter, 使用 ECDHE 曲线 <curve> 的成功 TLS 连接总数
   ssl.sigalgs.<sigalg>, Counter, 使用签名算法 <sigalg> 的成功 TLS 连接总数
   ssl.versions.<version>, Counter, 使用协议版本 <version> 的成功 TLS 连接总数

.. _config_listener_stats_per_handler:

每个处理程序 (handler) 的监听器统计信息
--------------------------------------------

每个监听器还有一个基于 *listener.<address>.<handler>.* 的统计树，它包含 *per-handler* 的统计信息。如 :ref:`线程模型 <arch_overview_threading>` 文档中所述，Envoy 的线程模型包括主线程以及由 --concurrency 选项控制的多个工作线程。按照这些原则，<handler> 等于 main_thread，worker_0，worker_1 等。这些统计信息可用于寻找接受或活动连接上每个处理程序/工作程序 (handler/worker) 的不平衡状况。

.. csv-table::
   :header: 名称, 类型, 描述
   :widths: 1, 1, 2

   downstream_cx_total, Counter, 这个处理程序上的连接总数
   downstream_cx_active, Gauge, 这个处理程序上活跃的连接总数

.. _config_listener_manager_stats:

监听器管理器
--------------------

监听管理器有一个以 *listener_manager.* 为根的统计树，其中包含以下统计信息：统计信息名称中的任何 ``:`` 字符均替换为 ``_``。

.. csv-table::
   :header: 名称, 类型, 描述
   :widths: 1, 1, 2

   listener_added, Counter, 添加的监听器总数（不管是通过静态配置还是 LDS)
   listener_modified, Counter, 修改过的监听器总数（通过 LDS）
   listener_removed, Counter, 删除过的监听器总数（通过 LDS）
   listener_stopped, Counter, 停止的监听器总数
   listener_create_success, Counter, 成功添加到 workers 的监听器对象总数
   listener_create_failure, Counter, 添加到 workers 失败的监听器对象总数
   listener_in_place_updated, Counter, 创建以执行过滤器链更新路径的监听器听器对象总数
   total_filter_chains_draining, Gauge, 当前正在耗尽的过滤器链的数量
   total_listeners_warming, Gauge, 当前预热的监听器数量
   total_listeners_active, Gauge, 当前活动的监听器数量
   total_listeners_draining, Gauge, 当前正在耗尽的监听器数量
   workers_started, Gauge, 布尔值（如果已启动则为 1，否则为 0），指示是否已在 worker 上初始化监听器
