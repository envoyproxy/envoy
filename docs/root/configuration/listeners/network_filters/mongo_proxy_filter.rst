.. _config_network_filters_mongo_proxy:

Mongo 代理
===========

* MongoDB :ref:`架构概览 <arch_overview_mongo>`
* :ref:`v3 API 参考 <envoy_v3_api_msg_extensions.filters.network.mongo_proxy.v3.MongoProxy>`
* 过滤器的名称应该配置为 *envoy.filters.network.mongo_proxy* 。

.. _config_network_filters_mongo_proxy_fault_injection:

故障注入
---------------

Mongo 代理过滤器支持故障注入。通过查看 v3 API 参考如何配置。

.. _config_network_filters_mongo_proxy_stats:

统计
----------

每个配置的 MongoDB 代理过滤器都有以 *mongo.<stat_prefix>.* 为根，且有如下所示的统计信息：

.. csv-table::
  :header: 名称, 类型, 描述
  :widths: 1, 1, 2

  decoding_error, Counter, MongoDB 协议解码异常数
  delay_injected, Counter, 注入延迟的次数
  op_get_more, Counter, OP_GET_MORE 信息数
  op_insert, Counter, OP_INSERT 信息数
  op_kill_cursors, Counter, OP_KILL_CURSORS 信息数
  op_query, Counter, OP_QUERY 信息数
  op_query_tailable_cursor, Counter, 设置可裁剪光标标志的 OP_QUERY 数
  op_query_no_cursor_timeout, Counter, 未设置光标超时标志的 OP_QUERY 数
  op_query_await_data, Counter, 设置等待数据标志的 OP_QUERY 数
  op_query_exhaust, Counter, 设置耗尽标志的 OP_QUERY 数
  op_query_no_max_time, Counter, 未设置 maxTimeMS 的查询数
  op_query_scatter_get, Counter, 分散获取查询数
  op_query_multi_get, Counter, 多重获取查询数
  op_query_active, Gauge, 活跃查询数
  op_reply, Counter, OP_REPLY 信息数
  op_reply_cursor_not_found, Counter, 设置未找到光标标志的 OP_REPLY 数
  op_reply_query_failure, Counter, 设置查询失败标志的 OP_REPLY 数
  op_reply_valid_cursor, Counter, 有效光标的 OP_REPLY 数
  cx_destroy_local_with_active_rq, Counter, 使用活跃查询在本地销毁连接
  cx_destroy_remote_with_active_rq, Counter, 使用活跃查询在远程销毁连接
  cx_drain_close, Counter, 在服务器耗尽期间，连接在边界上正常关闭数

分散获取
^^^^^^^^^^^^

Envoy 将 *scatter get* 定义为不使用 *_id* 字段作为查询参数的任何查询。
Envoy 在最高级文档和 *$query* 字段中查找 *_id* 。

批量获取
^^^^^^^^^^

Envoy 将 *multi get* 定义为使用 *_id* 字段作为查询参数的任何查询，但其中 *_id* 不是标量（scalar）值（例如文档或数组）。Envoy 在最高级文档和 *$query* 字段中查找 *_id* 。

.. _config_network_filters_mongo_proxy_comment_parsing:

$comment 解析
^^^^^^^^^^^^^^^^

如果一个请求中包含一个顶级的 *$comment* 字段（通常除了 *$query* 字段），Envoy 将会把他解析为 JSON 并如下结构所示：

.. code-block:: json

  {
    "callingFunction": "..."
  }

callingFunction
  *(required, string)* 进行查询的函数。如果可用，这个函数将会被用于在 :ref:`调用点 <config_network_filters_mongo_proxy_callsite_stats>` 查询统计信息。

命令统计
^^^^^^^^^^^^^^^^^^^^^^

MongoDB 过滤器将收集在 *mongo.<stat_prefix>.cmd.<cmd>.* 命名空间中命令的统计信息。

.. csv-table::
  :header: 名称, 类型, 描述
  :widths: 1, 1, 2

  total, Counter, 命令数
  reply_num_docs, Histogram, 回复文件数
  reply_size, Histogram, 回复字节大小
  reply_time_ms, Histogram, 以毫秒为单位的命令时间

.. _config_network_filters_mongo_proxy_collection_stats:

集合统计查询
^^^^^^^^^^^^^^

MongoDB 过滤器将收集在 *mongo.<stat_prefix>.collection.<collection>.query.* 命名空间中查询的统计信息。

.. csv-table::
  :header: 名称, 类型, 描述
  :widths: 1, 1, 2

  total, Counter, 查询数
  scatter_get, Counter, 分散获取数
  multi_get, Counter, 多重获取数
  reply_num_docs, Histogram, 回复文件数
  reply_size, Histogram, 回复字节大小
  reply_time_ms, Histogram, 以毫秒为单位的查询时间

.. _config_network_filters_mongo_proxy_callsite_stats:

每个集合和调用点查询统计信息
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

如果应用在 *$comment* 字段中提供了 :ref:`调用函数 <config_network_filters_mongo_proxy_comment_parsing>`，Envoy 将会生成每个调用点的统计信息。
这些统计信息与 :ref:`每个集合的统计信息 <config_network_filters_mongo_proxy_collection_stats>` 相互匹配，但这仅会在 *mongo.<stat_prefix>.collection.<collection>.callsite.<callsite>.query.* 命名空间中找到。

.. _config_network_filters_mongo_proxy_runtime:

运行时
-------

Mongo 代理过滤器支持如下所示的运行时设置：

mongo.connection_logging_enabled
  连接的百分比表示将会启用日志记录。默认为 100。这仅表示只有一部分的连接会被记录日志，但是所有连接的信息都会被记录日志。

mongo.proxy_enabled
  连接的百分比表示将会启用代理。默认为 100。

mongo.logging_enabled
  消息的百分比表示将会被日志记录。默认为 100。如果少于 100，则可以只记录查询而不进行答复等。

mongo.mongo.drain_close_enabled
  连接的百分比表示如果服务器正在关闭，则将关闭连接，否则将尝试关闭连接。默认为 100。

mongo.fault.fixed_delay.percent
  当没有响应故障时，合格的 MongoDB 操作会受注入故障影响的概率。默认为配置中指定的 *percentage* 。 

mongo.fault.fixed_delay.duration_ms
  延迟持续时间为毫秒。默认为配置中指定的 *duration_ms* 。

访问日志格式
-----------------

访问日志格式不能定制，如下所示：

.. code-block:: json

  {"time": "...", "message": "...", "upstream_host": "..."}

time
  解析完整消息的系统时间，包括毫秒。

message
  消息的文本扩展。消息是否完全扩展取决于上下文。有时会显示总结数据避免非常大的日志大小。

upstream_host
  连接到代理的上游地址，如果可用。如果过滤器与 :ref:`TCP 代理过滤器<config_network_filters_tcp_proxy>` 一同使用则会被填充。

.. _config_network_filters_mongo_proxy_dynamic_metadata:

动态元数据
----------------

当通过 :ref:`配置 <envoy_v3_api_field_extensions.filters.network.mongo_proxy.v3.MongoProxy.emit_dynamic_metadata>` 启用 Mongo 过滤器时，Mongo 过滤器将会发出以下元数据。
这些动态元数据以键-值对来使用，其中键表示正在访问的数据库和集合，而值是对集合执行的操作的列表。

.. csv-table::
  :header: 名称, 类型, 描述
  :widths: 1, 1, 2

  key, string, *db.collection* 格式资源名称。
  value, array, 表示在资源上执行操作的字符串列表（插入/更新/查询/删除）。
