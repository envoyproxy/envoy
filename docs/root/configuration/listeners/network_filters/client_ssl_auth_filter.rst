.. _config_network_filters_client_ssl_auth:

客户端 TLS 认证
================

* 客户端 TLS 认证过滤器 :ref:`架构概述 <arch_overview_ssl_auth_filter>`
* :ref:`v3 API 参考 <envoy_v3_api_msg_extensions.filters.network.client_ssl_auth.v3.ClientSSLAuth>`
* 此过滤器的名称应该配置为 *envoy.filters.network.client_ssl_auth* 。

.. _config_network_filters_client_ssl_auth_stats:

统计
-----

每一个配置的客户端 TLS 认证过滤器都有一个基于 *auth.clientssl.<stat_prefix>.* 的统计信息，统计信息如下所示：

.. csv-table::
  :header: 名称, 类型, 描述
  :widths: 1, 1, 2

  update_success, Counter, 更新成功的总数
  update_failure, Counter, 更新失败的总数
  auth_no_ssl, Counter, 没有 TLS 而忽略的连接总数
  auth_ip_allowlist, Counter, IP allowlist 允许的连接总数
  auth_digest_match, Counter, 证书匹配通过的连接总数
  auth_digest_no_match, Counter, 证书匹配未通过的连接总数
  total_principals, Gauge, 加载总数

.. _config_network_filters_client_ssl_auth_rest_api:

REST API
--------

.. http:get:: /v1/certs/list/approved

  认证过滤器会在每次刷新间隔中调用这些 API 接口以获取当前已批准的证书（certificates）/主体（principals）列表。预期的 JSON 响应如下所示：

  .. code-block:: json

    {
      "certificates": []
    }

  certificates
    *(required, array)* 获取的证书/主体列表。

  每个证书对象被定义为：

  .. code-block:: json

    {
      "fingerprint_sha256": "...",
    }

  fingerprint_sha256
    *(required, string)* 已批准的客户端证书的 SHA256 哈希值。Envoy 会将该哈希值与所提交的客户端证书进行匹配，以确定是否存在摘要匹配。
