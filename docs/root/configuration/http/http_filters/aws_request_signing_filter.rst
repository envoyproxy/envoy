
.. _config_http_filters_aws_request_signing:

AWS 请求签名
===================

* :ref:`v3 API 参考 <envoy_v3_api_msg_extensions.filters.http.aws_request_signing.v3.AwsRequestSigning>`
* 此过滤器的名称应该被配置为 *envoy.filters.http.aws_request_signing* 。


.. attention::

  AWS 请求签名过滤器是实验性的，目前正在积极开发中。

HTTP AWS 请求签名过滤器用于访问经过身份验证的 AWS 服务。它使用现有的 AWS 凭证提供程序获取用于生成所需标头的密文。

示例配置
---------

过滤器配置示例：

.. code-block:: yaml

  name: envoy.filters.http.aws_request_signing
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.filters.http.aws_request_signing.v3.AwsRequestSigning
    service_name: s3
    region: us-west-2

统计
------

AWS 请求签名过滤器在 *http.<stat_prefix>.aws_request_signing.* 命名空间中输出统计信息。
:ref:`stat 前缀 <envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.stat_prefix>`
来自所属的 HTTP 连接管理器。

.. csv-table::
  :header: 名称, 类型, 描述
  :widths: 1, 1, 2

  signing_added, Counter, 添加到请求的身份验证头部总数
  signing_failed, Counter, 未添加签名的请求总数
