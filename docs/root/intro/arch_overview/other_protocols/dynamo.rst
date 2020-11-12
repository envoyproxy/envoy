.. _arch_overview_dynamo:

DynamoDB
========

Envoy 支持具有以下功能的 HTTP 级别 DynamoDB 过滤器：

* DynamoDB API 请求/响应解析器。
* DynamoDB 每个操作/表/分区和操作统计。
* 4xx 响应的失败类型统计信息，从响应 JSON 中解析，例如 ProvisionedThroughputExceededException。
* 批量操作部分的失败统计。

DynamoDB 过滤器是 Envoy 在 HTTP 层的可扩展性和核心抽象的一个很好的例子。在 Lyft 中，我们使用此过滤器与 DynamoDB 进行的所有应用程序通信。 它提供了一个与使用中的应用程序平台和特定的 AWS SDK 无关的宝贵数据源。

DynamoDB 过滤器 :ref:`配置 <config_http_filters_dynamo>`。
