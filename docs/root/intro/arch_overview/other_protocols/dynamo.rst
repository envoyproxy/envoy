.. _arch_overview_dynamo:

DynamoDB
========

Envoy 支持 HTTP 级别 DynamoDB 过滤器，其具有以下功能：

* DynamoDB API 请求/响应解析器。
* DynamoDB 每个操作/表/分区和操作统计。
* 解析响应 JSON，统计 4xx 类型的响应错误，例如 ProvisionedThroughputExceededException。
* 批量操作中部分失败的统计。

DynamoDB 过滤器是 Envoy 在 HTTP 层的可扩展性和核心抽象的一个很好的例子。在 Lyft 中，我们对和 DynamoDB 进行的所有应用进程的通信都使用此过滤器。它提供了一个与使用中的应用程序平台和特定的 AWS SDK 无关的宝贵数据源。

