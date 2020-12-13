.. _config_network_filters_zookeeper_proxy:

ZooKeeper 代理
===============

ZooKeeper 代理过滤器解码 `Apache ZooKeeper <https://zookeeper.apache.org/>`_ 的客户端协议。它能够对负载中的请求、响应、事件进行解码。其中支持 `ZooKeeper 3.5 <https://github.com/apache/zookeeper/blob/master/zookeeper-server/src/main/java/org/apache/zookeeper/ZooDefs.java>`_  中已知的大多数操作码。不支持的那就是与 SALS 认证有关。

.. attention::

   ZooKeeper 代理过滤器目前在积极的开发中，还处于试验阶段。随着时间的推移，功能将会得到扩展，并且配置结构也可能会发生变化。

.. _config_network_filters_zookeeper_proxy_config:

配置
------

ZooKeeper 代理过滤器应该与 TCP 代理过滤器相连接，如下面的配置片段所示：

.. code-block:: yaml

  filter_chains:
  - filters:
    - name: envoy.filters.network.zookeeper_proxy
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.filters.network.zookeeper_proxy.v3.ZooKeeperProxy
        stat_prefix: zookeeper
    - name: envoy.filters.network.tcp_proxy
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.filters.network.tcp_proxy.v3.TcpProxy
        stat_prefix: tcp
        cluster: ...


.. _config_network_filters_zookeeper_proxy_stats:

统计
------

每一个ZooKeeper 代理过滤器的配置都有一个基于 *<stat_prefix>.zookeeper.* 的统计信息。可用的计数器如下：

.. csv-table::
  :header: 名称, 类型, 描述
  :widths: 1, 1, 2

  decoder_error, Counter, 消息未解码的次数
  request_bytes, Counter, 解码请求信息中的字节数
  connect_rq, Counter, 常规连接（非只读）请求数
  connect_readonly_rq, Counter, 设置了 readonly 标志的连接请求数
  ping_rq, Counter, Ping 请求的数量
  auth.<type>_rq, Counter, 给定类型的身份认证的请求数量
  getdata_rq, Counter, 获取请求数据的数量
  create_rq, Counter, create 请求数
  create2_rq, Counter, create2 请求数
  setdata_rq, Counter, setdata 请求数
  getchildren_rq, Counter, getchildren 请求数
  getchildren2_rq, Counter, getchildren2 请求数
  remove_rq, Counter, 删除请求数
  exists_rq, Counter, 统计请求数
  getacl_rq, Counter, getacl 请求数
  setacl_rq, Counter, setacl 请求数
  sync_rq, Counter, 同步请求数
  multi_rq, Counter, 多笔交易请求数
  reconfig_rq, Counter, 重新配置请求数
  close_rq, Counter, 关闭请求数
  setwatches_rq, Counter, setwatches 请求数
  checkwatches_rq, Counter, checkwatches 请求数
  removewatches_rq, Counter, removewatches 请求数
  check_rq, Counter, 检查请求数
  response_bytes, Counter, 解码响应消息中的字节数
  connect_resp, Counter, 连接响应数
  ping_resp, Counter, Ping 响应数
  auth_resp, Counter, 认证响应数
  watch_event, Counter, 服务器触发的监视事件的数量
  getdata_resp, Counter, getdata 响应数
  create_resp, Counter, create 响应数
  create2_resp, Counter, create2 响应数
  createcontainer_resp, Counter, createcontainer 响应数
  createttl_resp, Counter, createttl 响应数
  setdata_resp, Counter, setdata 响应数
  getchildren_resp, Counter, getchildren 响应数
  getchildren2_resp, Counter, getchildren2 响应数
  getephemerals_resp, Counter, getephemerals 响应数
  getallchildrennumber_resp, Counter, getallchildrennumber 响应数
  remove_resp, Counter, 删除响应数
  exists_resp, Counter, 存在响应数
  getacl_resp, Counter, getacl 响应数
  setacl_resp, Counter, setacl 响应数
  sync_resp, Counter, 同步响应数
  multi_resp, Counter, 多笔交易响应数
  reconfig_resp, Counter, 重新配置响应数
  close_resp, Counter, 关闭响应数
  setauth_resp, Counter, setauth 响应数
  setwatches_resp, Counter, setwatches 响应数
  checkwatches_resp, Counter, checkwatches 响应数
  removewatches_resp, Counter, removewatches 响应数
  check_resp, Counter, 检查响应数


.. _config_network_filters_zookeeper_proxy_latency_stats:

操作码延迟统计信息
-------------------

过滤器将在 *<stat_prefix>.zookeeper.<opcode>_response_latency* 命名空间中收集延迟统计信息。延迟统计信息以毫秒为单位：

.. csv-table::
  :header: 名称, 类型, 描述
  :widths: 1, 1, 2

  connect_response_latency, Histogram, 操作码执行时间（毫秒）
  ping_response_latency, Histogram, 操作码执行时间（毫秒）
  auth_response_latency, Histogram, 操作码执行时间（毫秒）
  watch_event, Histogram, 操作码执行时间（毫秒）
  getdata_response_latency, Histogram, 操作码执行时间（毫秒）
  create_response_latency, Histogram, 操作码执行时间（毫秒）
  create2_response_latency, Histogram, 操作码执行时间（毫秒）
  createcontainer_response_latency, Histogram, 操作码执行时间（毫秒）
  createttl_response_latency, Histogram, 操作码执行时间（毫秒）
  setdata_response_latency, Histogram, 操作码执行时间（毫秒）
  getchildren_response_latency, Histogram, 操作码执行时间（毫秒）
  getchildren2_response_latency, Histogram, 操作码执行时间（毫秒）
  getephemerals_response_latency, Histogram, 操作码执行时间（毫秒）
  getallchildrennumber_response_latency, Histogram, 操作码执行时间（毫秒）
  remove_response_latency, Histogram, 操作码执行时间（毫秒）
  exists_response_latency, Histogram, 操作码执行时间（毫秒）
  getacl_response_latency, Histogram, 操作码执行时间（毫秒）
  setacl_response_latency, Histogram, 操作码执行时间（毫秒）
  sync_response_latency, Histogram, 操作码执行时间（毫秒）
  multi_response_latency, Histogram, 操作码执行时间（毫秒）
  reconfig_response_latency, Histogram, 操作码执行时间（毫秒）
  close_response_latency, Histogram, 操作码执行时间（毫秒）
  setauth_response_latency, Histogram, 操作码执行时间（毫秒）
  setwatches_response_latency, Histogram, 操作码执行时间（毫秒）
  checkwatches_response_latency, Histogram, 操作码执行时间（毫秒）
  removewatches_response_latency, Histogram, 操作码执行时间（毫秒）
  check_response_latency, Histogram, 操作码执行时间（毫秒）


.. _config_network_filters_zookeeper_proxy_dynamic_metadata:

动态元数据
----------------

ZooKeeper 过滤器分析每一个消息都会释放出以下动态元数据：

.. csv-table::
  :header: 名称, 类型, 描述
  :widths: 1, 1, 2

  <path>, string, 与请求、响应和事件关联的路径
  <opname>, string, 与请求、响应和事件关联的操作名称
  <create_type>, string, 用于 znode 标志的字符串表示形式
  <bytes>, string, 以字节为单位欸的请求消息的大小
  <watch>, string, 如果设置监听则为 True，否则为 False
  <version>, string, 请求中给定的 version 参数（如果有）
  <timeout>, string, 连接响应中的超时参数
  <protocol_version>, string, 连接响应中的协议版本
  <readonly>, string, 连接响应中的 readonly 标志
  <zxid>, string, 响应头中的 zxid 字段
  <error>, string, 响应头中的 error 字段
  <client_state>, string, 监听事件中的 state 字段
  <event_type>, string, 监听事件中的事件类型
