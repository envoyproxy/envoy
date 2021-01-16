.. _config_http_filters_dynamo:

DynamoDB
========

* DynamoDB :ref:`架构概述 <arch_overview_dynamo>`
* :ref:`v3 API 参考 <envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpFilter.name>`
* 该过滤器应该用 *envoy.filters.http.dynamo* 来配置。

统计
----------

DynamoDB 过滤器在 *http.<stat_prefix>.dynamodb.* 命名空间中输出统计数据. :ref:`统计前缀
<envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.stat_prefix>` 来自于自己的 HTTP 连接管理器。

每个操作的统计可以在 *http.<stat_prefix>.dynamodb.operation.<operation_name>.* 命名空间中找到。

  .. csv-table::
    :header: 名称, 类型, 描述
    :widths: 1, 1, 2

    upstream_rq_total, Counter, 带有 <operation_name> 的请求总数
    upstream_rq_time, Histogram, 在 <operation_name> 上的耗时
    upstream_rq_total_xxx, Counter, 每个响应码 (如 503/2xx/ 等) 中带有 <operation_name> 的请求总数
    upstream_rq_time_xxx, Histogram, 每个响应码 (如 400/3xx/ 等) 在 <operation_name> 上的耗时

每张表的统计数据可以在 *http.<stat_prefix>.dynamodb.table.<table_name>.* 命名空间中找到。
对 DynamoDB 的大多数操作都只涉及到一张表, 但是 BatchGetItem 和 BatchWriteItem 可以包括几张表, 在
这种情况下, Envoy 只有在批处理的所有操作都使用同一张表时才会跟踪每张表的统计。

  .. csv-table::
    :header: 名称, 类型, 描述
    :widths: 1, 1, 2

    upstream_rq_total, Counter, 在 <table_name> 表上的请求总数
    upstream_rq_time, Histogram, 在 <table_name> 表上的耗时
    upstream_rq_total_xxx, Counter, 每个响应码 (如 503/2xx/ 等) 在 <table_name> 表上的请求总数
    upstream_rq_time_xxx, Histogram, 每个响应码 (如 400/3xx/ 等) 在 <table_name> 表上的耗时

*声明: 请注意，这是 Amazon DynamoDB 的预发布功能，尚未广泛使用。*
每个分区和操作统计可以在 *http.<stat_prefix>.dynamodb.table.<table_name>.* 命名空间中找到。
对于批处理操作，只有当所有操作中使用的是同一张表时，Envoy 才会跟踪每个分区和操作统计。

  .. csv-table::
    :header: 名称, 类型, 描述
    :widths: 1, 1, 2

    capacity.<operation_name>.__partition_id=<last_seven_characters_from_partition_id>, Counter, 在给定的 <partition_id> 中的 <table_name> 表上 <operation_name> 的容量总数

其它的详细统计:

* 对于 4xx 响应和部分批量操作失败，给定表的失败总数和失败情况在 *http.<stat_prefix>.dynamodb.error.<table_name>.* 命名空间中进行跟踪。

  .. csv-table::
    :header: 名称, 类型, 描述
    :widths: 1, 1, 2

    <error_type>, Counter, 给定的 <table_name> 表上的特定 <error_type> 的总数
    BatchFailureUnprocessedKeys, Counter, 给定的 <table_name> 表上的部分批处理失败的总数

运行时
-------

DynamoDB 过滤器支持下面的运行时设置:

dynamodb.filter_enabled
  启用了过滤器的请求的百分比。默认值为 100％。
