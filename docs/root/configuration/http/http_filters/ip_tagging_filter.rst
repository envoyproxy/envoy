.. _config_http_filters_ip_tagging:

IP 标记
==========

HTTP IP 标记过滤器为来自 :ref:`x-forwarded-for <config_http_conn_man_headers_x-forwarded-for>` 的受信任地址使用字符串标记
来设置头部 *x-envoy-ip-tags*。如果地址没有标记，则不设置头部。

IP 标记的实现提供了一种可伸缩的方法，可以高效地将 IP 地址与大量的 CIDR 进行比较。存储标签和 IP 地址子网的底层算法是 S.Nilsson 和
G.Karlsson 在论文 `使用 LC 尝试查找 IP 地址 <https://www.nada.kth.se/~snilsson/publications/IP-address-lookup-using-LC-tries/>`_
中描述的一种压缩 tire 树。

配置
--------

* :ref:`v3 API 参考 <envoy_v3_api_msg_extensions.filters.http.ip_tagging.v3.IPTagging>`
* 此过滤器的名称应该被配置为 *envoy.filters.http.ip_tagging*。

统计
----------

IP 标记过滤器在 *http.<stat_prefix>.ip_tagging.* 命名空间中输出统计信息。stat 前缀来自拥有的 HTTP 连接管理器。

.. csv-table::
  :header: 名称, 类型, 描述
  :widths: 1, 1, 2

        <tag_name>.hit, Counter, 应用了 <tag_name> 的请求总数
        no_hit, Counter, 不适用 IP 标记的请求总数
        total, Counter, IP 标记过滤器操作的请求总数

运行时
---------

IP 标记过滤器支持以下运行时设置：

ip_tagging.http_filter_enabled
    启用过滤器的请求的百分比。默认值为 100。
