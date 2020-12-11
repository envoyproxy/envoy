.. _config_listener_filters_tls_inspector:

TLS 检查器
=============

TLS 检查器监听器过滤器（TLS Inspector listener filter）可以检测传输是 TLS 还是纯文本，如果是 TLS，它检查来自客户端的 `服务器名称指示 <https://en.wikipedia.org/wiki/Server_Name_Indication>`_ 和/或者 `应用层协议谈判 <https://en.wikipedia.org/wiki/Application-Layer_Protocol_Negotiation>`_。它可以用来通过 :ref:`FilterChainMatch <envoy_v3_api_msg_config.listener.v3.FilterChainMatch>` 的 :ref:`server_names <envoy_v3_api_field_config.listener.v3.FilterChainMatch.server_names>` 和/或者 :ref:`application_protocols <envoy_v3_api_field_config.listener.v3.FilterChainMatch.application_protocols>` 来选择一个 :ref:`FilterChain <envoy_v3_api_msg_config.listener.v3.FilterChain>` 。

* :ref:`SNI <faq_how_to_setup_sni>`
* :ref:`v2 API 参考 <envoy_v3_api_field_config.listener.v3.ListenerFilter.name>`
* 这个过滤器应该使用名字 *envoy.filters.listener.tls_inspector* 来配置。

示例
-------

过滤器示例配置可以是：

.. code-block:: yaml

  listener_filters:
  - name: "envoy.filters.listener.tls_inspector"
    typed_config: {}

统计
----------

该过滤器有一个统计树，其根为 *tls_inspector* 并具有如下统计信息：

.. csv-table::
  :header: 名称, 类型, 描述
  :widths: 1, 1, 2

  connection_closed, 计数器, 关闭的连接总数
  client_hello_too_large, 计数器, 收到的不合理的大 Client Hello 总数
  read_error, 计数器, 读错误的总数
  tls_found, 计数器, 发现 TLS 的总次数
  tls_not_found, 计数器, 未发现 TLS 的总次数
  alpn_found, 计数器, `应用层协议谈判 <https://en.wikipedia.org/wiki/Application-Layer_Protocol_Negotiation>`_ 成功的总次数
  sni_found, 计数器, `应用层协议谈判 <https://en.wikipedia.org/wiki/Application-Layer_Protocol_Negotiation>`_ 失败的总次数
  sni_not_found, 计数器, 未发现 `服务器名称指示 <https://en.wikipedia.org/wiki/Server_Name_Indication>`_ 的总次数
