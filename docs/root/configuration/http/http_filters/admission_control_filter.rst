.. _config_http_filters_admission_control:

准入控制
=================

.. attention::

  准入控制过滤器属于实验性功能，目前正在积极开发中。

有关每个配置项的详细信息，请参见 :ref:`v3 API 参考 <envoy_v3_api_msg_extensions.filters.http.admission_control.v3alpha.AdmissionControl>`。

概览
--------

准入控制过滤器会根据滑动时间窗口内之前请求的成功率，概率性的拒绝请求。它基于 `谷歌 SRE 手册 <https://landing.google.com/sre/sre-book/toc/index.html>`_ 中的
`客户端侧节流 <https://landing.google.com/sre/sre-book/chapters/handling-overload/>`_。唯一值得一提的关于准入控制过滤器的降载
与客户端侧节流定义的降载的区别是：用户可以配置触发主动降载的成功率阈值。用户还可以通过配置成功请求的定义来计算拒绝的概率。

过滤器将按照如下的方式来计算拒绝请求的概率：

.. math::

   P_{reject} = {(\frac{n_{total} - s}{n_{total} + 1})}^\frac{1}{aggression}

其中,

.. math::

   s = \frac{n_{success}}{threshold}


- *n* 指一个滑动窗口内的请求量总和。
- *threshold* 是一个可配置的值，该值表示过滤器将 **不再拒绝** 请求的最低请求成功率。该值被归一化为 [0,1] 进行计算。
- *aggression* 控制拒绝率曲线，因此 1.0 表示拒绝率随成功率降低而线性增加。当 **aggression** 增大时，
  随着成功率增大，拒绝概率相对更大。更详细的解释请参见 `Aggression`_。

.. note::
   成功率计算基于每个线程单独执行，以提升性能。此外，线程间隔离也可以降低单个不良连接引起的成功率异常的影响范围。因此，
   各工作线程间的拒绝率可能不相等。

.. note::
   健康检查流量不会计入过滤器的任何测量结果。

关于此参数的更详细信息，请参见 :ref:`v3 API 参考
<envoy_v3_api_msg_extensions.filters.http.admission_control.v3alpha.AdmissionControl>`。

成功请求的定义是一个 :ref:`可配参数
<envoy_v3_api_msg_extensions.filters.http.admission_control.v3alpha.AdmissionControl.SuccessCriteria>`，对 HTTP
和 gRPC 请求都可以配置。

Aggression
~~~~~~~~~~

关于 aggression 的值对拒绝率的影响，如下图所示：

.. image:: images/aggression_graph.png

由于第一张图中成功率阈值被设置为 95%，因此在低于此值前拒绝率一直保持为 0。在第二张图中，成功率低于 50% 前，三个拒绝率一直
保持为 0。在所有的情况下，当成功率跌至 0% 时，拒绝率将达到一个接近 100% 的值。aggression 的值决定了在指定的成功率下，拒绝率
将达到多高，因此它将更 *积极的* 降载。

配置示例
---------------------
如下所示，这是一个过滤器的配置示例。并非所有字段都是必要的，并且许多字段都可以通过运行时设置来覆盖。

.. code-block:: yaml

  name: envoy.filters.http.admission_control
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.filters.http.admission_control.v3alpha.AdmissionControl
    enabled:
      default_value: true
      runtime_key: "admission_control.enabled"
    sampling_window: 120s
    sr_threshold:
      default_value: 95.0
      runtime_key: "admission_control.sr_threshold"
    aggression:
      default_value: 1.5
      runtime_key: "admission_control.aggression"
    success_criteria:
      http_criteria:
        http_success_status:
          - start: 100
            end:   400
          - start: 404
            end:   404
      grpc_criteria:
        grpc_success_status:
          - 0
          - 1

上述配置可以理解为：

* 计算请求成功率的滑动窗口是 120 秒。
* 在滑动窗口内的请求成功率低于 95% 前，不执行降载。
* HTTP 请求响应为 1xx、 2xx、 3xx、或 404 时，将被视为成功请求。
* gRPC 请求响应为 OK 或 CANCELLED 时，将被视为成功请求。

统计
----------
准入控制过滤器将统计信息输出在 *http.<stat_prefix>.admission_control.* 命名空间下。
:ref:`stat prefix <envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.stat_prefix>`
来自于所属的 HTTP 连接管理器。

.. csv-table::
  :header: 名称, 类型, 描述
  :widths: auto

  rq_rejected, Counter, 过滤器未接受的请求总数。
  rq_success, Counter, 被视为成功的请求总数。
  rq_failure, Counter, 被视为失败的请求总数。
