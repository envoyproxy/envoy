.. _config_http_filters_adaptive_concurrency:

自适应并发
====================

.. attention::

  自适应过滤器还是实验性的，目前正在积极开发中

这个过滤器应该通过名字 `envoy.filters.http.adaptive_concurrency` 配置

查看 :ref:`v3 API 参考 <envoy_v3_api_msg_extensions.filters.http.adaptive_concurrency.v3.AdaptiveConcurrency>` 来获取各配置参数的详细信息。

概览
--------
自适应并发过滤器可以在任何时候动态调整连接到给定集群中所有主机的允许未完成（并发）的请求数。并发值
的计算方法是使用已完成请求的延迟采样，然后将时间窗口中的测量样本与集群中主机的预期延迟进行比较。

并发控制器
------------
并发控制器实现了负责为每个请求做出转发决策
并记录在并发限制计算中使用的延迟采样的算法。

梯度控制器
~~~~~~~~~~~~~~~~~~~
梯度控制器基于上游理想往返时间（minRTT）周期性地测量做转发决策。

:ref:`v3 API 参考 <envoy_v3_api_msg_extensions.filters.http.adaptive_concurrency.v3.GradientControllerConfig>`

计算 minRTT
^^^^^^^^^^^^^^^^^^^^^^

minRTT 通过只允许一个非常低的未完成请求计数来周期性地度量
上游集群在这些理想条件下延迟。计算也是
当并发限制被确定为可能的最小值时触发
5个连续的采样窗口。minRTT 计算窗口的长度是根据请求的数量可变的
，过滤器被配置为聚合以表示预期的请求上游延迟。

一个可配置的 *jitter* 值用于随机延迟开始 minRTT 计算窗口的
时间。这是不必要的，可以禁用; 但是，建议这样做
防止集群中的所有主机处于 minRTT 计算窗口中(并且有一个并发限制，默认为3)同时。
抖动有助于再开启重试情况下抵消在下行成功率技术上 minRTT 的影响

在 minRTT 测量期间，在测量窗口可能有明显的 503 请求增加，因为并发限制可能显著下降。这是意料之中的事
建议启用 resets/503 的重试。

.. note::

    建议使用 :ref:`the previous_hosts retry predicate
    <arch_overview_http_retry_plugins>`. minRTT 重新计算抖动, 不太可能
    集群中的所有主机都将处于 minRTT 计算窗口中，因此在不同的主机上重新尝试
    在这个场景下，在集群中成功的可能性更高。

一旦计算出来，minRTT 就被用于计算一个称为*梯度*的值.

梯度
^^^^^^^^^^^^
梯度使用汇总的抽样请求延迟来计算 (sampleRTT):

.. math::

    gradient = \frac{minRTT + B}{sampleRTT}

这个梯度值有一个有用的属性，它会随着抽样延迟的增加而减少。
注意，添加到 minRTT 的缓冲区值 *B* 允许采样值的正态方差
通过要求采样的延迟超过某个可配置阈值的 minRTT在减小梯度值之前。

缓冲区是测量的 minRTT 值的百分比，该值是通过缓冲区字段修改的
:ref:`minRTT 计算参数 <envoy_v3_api_msg_extensions.filters.http.adaptive_concurrency.v3.GradientControllerConfig.MinimumRTTCalculationParams>` 
缓冲区的计算如下:

.. math::

    B = minRTT * buffer_{pct}

然后使用梯度值来更新并发限制:

.. math::

    limit_{new} = gradient * limit_{old} + headroom

并发限制空间
^^^^^^^^^^^^^^^^^^^^^^^^^^
事件发生时，头部值是增加并发限制的驱动因素，在sampleRTT 和 minRTT 差不多的情况下。
此值必须存在于 limit 计算中，因为它强制增加并发限制，直到出现偏离 minRTT 延迟。如果没有头部值，并发限制可能会停滞
如果 sampleRTT 和 minRTT 离得很近，则取一个不必要的小值。

因为头部值对于梯度控制器的适当功能是如此的必要，所以
头部值是不可配置的，并且固定在并发限制的平方根上。

限制
-----------
自适应并发过滤器的控制循环依赖于延迟测量以及基于这些度量对并发限制的调整。
因为这些，过滤器必须在它能完全控制的条件下工作并发请求。这意味着:

    1. 该过滤器在本地集群的过滤器链中按照预期工作

    2. 该过滤器必须能够限制集群的并发性。这意味着不能有指向群集的未被自适应并发过滤器解码的请求

示例配置
---------------------
下面可以找到一个过滤器配置示例。并不是所有的字段都是必需的
可以通过运行时设置覆盖字段。

.. code-block:: yaml

  name: envoy.filters.http.adaptive_concurrency
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.filters.http.adaptive_concurrency.v3.AdaptiveConcurrency
    gradient_controller_config:
      sample_aggregate_percentile:
        value: 90
      concurrency_limit_params:
        concurrency_update_interval: 0.1s
      min_rtt_calc_params:
        jitter:
          value: 10
        interval: 60s
        request_count: 50
    enabled:
      default_value: true
      runtime_key: "adaptive_concurrency.enabled"

以上配置的理解如下:

* 在100ms的时间窗口内收集延迟样本。当进入一个新窗口时，汇总请求 (sampleRTT) 和使用此 sampleRTT 更新并发限制
* 在计算 sampleRTT 时，使用该窗口所有采样延迟的 百分之 90
* 每隔 60 秒重新计算 minRTT ，并在开始处增加一个 0s-6s 的抖动(随机延迟) minRTT 重新计算。延迟由抖动值决定
* 收集 50个 请求样本来计算 minRTT，并使用百分之 90 来汇总它们
* 该过滤器默认启用

.. note::

    建议自适应并发过滤器在 filter 链中位于健康检查过滤器之后，以防止运行状况检查的延迟采样。
    如果对健康检查流量进行抽样，这可能会影响 minRTT 测量的准确性

运行时
-------

自适应并发过滤器支持以下运行时设置:

adaptive_concurrency.enabled
    重写自适应并发过滤器是否将使用并发控制器转发决策。如果设置为 `false`，该过滤器将是一个空操作。默认值
    在筛选器配置中指定为 `enabled`

adaptive_concurrency.gradient_controller.min_rtt_calc_interval_ms
    覆盖重新计算理想往返时间 (minRTT) 的时间间隔

adaptive_concurrency.gradient_controller.min_rtt_aggregate_request_count
    覆盖为计算 minRTT 而采样的请求数

adaptive_concurrency.gradient_controller.jitter
    覆盖 minRTT 计算开始时间引入的随机延迟。值为 `10` 表示为配置时间间隔的 10% 的随机延迟。指定的运行时值为
    固定到范围 [0,100]

adaptive_concurrency.gradient_controller.sample_rtt_calc_interval_ms
    重写基于抽样延迟重新计算并发限制的时间间隔
adaptive_concurrency.gradient_controller.max_concurrency_limit
    覆盖允许的最大并发限制

adaptive_concurrency.gradient_controller.min_rtt_buffer
    覆盖在计算并发限制时添加到 minRTT 的填充

adaptive_concurrency.gradient_controller.sample_aggregate_percentile
    在百分位数值计算中用于表示延迟样本集合。值 `95` 表示第 95 个百分位数。指定的运行时值为固定到范围 [0,100]

adaptive_concurrency.gradient_controller.min_concurrency
    覆盖在测量 minRTT 时固定的并发性

Statistics
----------
自适应并发过滤器将统计信息输出到
*http.<stat_prefix>.adaptive_concurrency.* namespace. :ref:`stat prefix
<envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.stat_prefix>`
来自所属的 HTTP connection manager. 统计信息是特定于并发性控制器的。

梯度控制器统计
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
梯度控制器使用命名空间
*http.<stat_prefix>.adaptive_concurrency.gradient_controller*.

.. csv-table::
  :header: Name, Type, Description
  :widths: auto

  rq_blocked, Counter, 被筛选器阻止的请求总数
  min_rtt_calculation_active, Gauge, 如果控制器正在进行 minRTT 计算，则设置为1，否则为 0 
  concurrency_limit, Gauge, 当前并发限制
  gradient, Gauge, 当前梯度值
  burst_queue_size, Gauge, 并发限制计算中的当前头部值
  min_rtt_msecs, Gauge, 当前测量的 minRTT 值
  sample_rtt_msecs, Gauge, 当前测量的 sampleRTT 集
