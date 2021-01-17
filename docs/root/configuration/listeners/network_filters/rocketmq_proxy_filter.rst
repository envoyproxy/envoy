.. _config_network_filters_rocketmq_proxy:

RocketMQ 代理
==============

Apache RocketMQ 是一个消息分发系统，它由四种角色组成：生产者、消费者、名称服务器（name server）和代理服务器（broker server）。前两者以 SDK 的形式嵌入到用户应用程序中，而后者则是独立的服务器。

RocketMQ 中的消息携带一个主题（topic）作为其目的地，并可选的携带一个或多个标记作为应用程序中特别的标签。

生产者通常根据它们的主题向代理发送消息。与大多数分发系统类似，生产者需要知道如何连接到它们的代理服务器。
为了实现这个目标，RocketMQ 提供了供生产者查找的名称服务器集群。也就是说，当生产者尝试去发送消息到一个新的主题，它首先会尝试从名称服务器中查找为该主题提供服务的代理的地址（称为路由信息）。
一但生产者获得了到达主题的路由信息，它就会主动的将他们缓存到内存中，并且会定期更新。这种机制虽然简单，但是可以保持服务的高可用性而不需要名称服务器服务的高可用性。

代理向最终用户提供消息服务。除了各种消息服务之外，它们还会定期向名称服务器报告当前服务的健康状态和主题的路由信息。

名称服务器的主要作用是查询主题的路由信息。此外，一旦所属的代理在配置的一段时间里没有报告其健康信息，它就会清除对应的路由信息条目。
这也是为了确保客户端可以一直连接到在线的且随时可以提供服务的代理。

应用程序使用消费者从代理拉取消息。它们之间也有类似心跳的机制来维持生命状态。RocketMQ 代理支持两种消息获取方法：long-pulling 和 pop 。

使用第一种方法，消费者必须实现负载均衡算法。从消费者的角度来看，pop 方法是无状态的。

Envoy RocketMQ 过滤器代理在生产者/消费者和代理之间的请求和响应。并且收集各种统计信息以增强可观测性。

目前，已经实现基于 pop 的消息拉取。而 Long-pulling 也将在下一个 pull request 中实现。

.. _config_network_filters_rocketmq_proxy_stats:

统计信息
----------

每个配置的 rocketmq 代理过滤器都有以 *rocketmq.<stat_prefix>.* 为根，如下所示的统计信息：

.. csv-table::
  :header: 名称, 类型, 描述
  :widths: 1, 1, 2

  request, Counter, 请求总数
  request_decoding_error, Counter, 解码异常请求总数
  request_decoding_success, Counter, 解码成功请求总数
  response, Counter, 响应总数
  response_decoding_error, Counter, 解码异常响应总数
  response_decoding_success, Counter, 解码成功响应总数
  response_error, Counter, 响应异常总数
  response_success, Counter, 响应成功总数
  heartbeat, Counter, 心跳请求总数
  unregister, Counter, 注销请求总数
  get_topic_route, Counter, 获取主题路由请求总数
  send_message_v1, Counter, 发送 v1 消息请求总数
  send_message_v2, Counter, 发送 v2 消息请求总数
  pop_message, Counter, 弹出消息请求总数
  ack_message, Counter, 确认消息请求总数
  get_consumer_list, Counter, 获取消费者列表请求总数
  maintenance_failure, Counter, 维修失败总数
  request_active, Gauge, 活跃请求总数
  send_message_v1_active, Gauge, 发送 v1 消息的活跃请求总数
  send_message_v2_active, Gauge, 发送 v2 消息的活跃请求总数
  pop_message_active, Gauge, 活跃弹出消息的活跃请求总数
  get_topic_route_active, Gauge, 获取主题路由的活跃请求总数
  send_message_pending, Gauge, 等到发送消息的请求总数
  pop_message_pending, Gauge, 等待弹出消息的请求总数
  get_topic_route_pending, Gauge, 等待获取主题路由的请求总数
  total_pending, Gauge, 等待请求总数
  request_time_ms, Histogram, 请求时间以毫秒为单位