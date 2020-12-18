.. _config_listener_filters_http_inspector:

HTTP 检视
==========

HTTP 检视监听器过滤器允许对应用程序协议是否为 HTTP 进行检测，如果是 HTTP，会进一步检测 HTTP 协议版本（HTTP/1.x 还是 HTTP/2）。这可以用来通过 :ref:`FilterChainMatch <envoy_v3_api_msg_config.listener.v3.FilterChainMatch>` 的  :ref:`application_protocols <envoy_v3_api_field_config.listener.v3.FilterChainMatch.application_protocols>` 来选择一个 :ref:`FilterChain <envoy_v3_api_msg_config.listener.v3.FilterChain>`。

* :ref:`监听器过滤器 v3 API 参考 <envoy_v3_api_msg_extensions.filters.listener.http_inspector.v3.HttpInspector>`
* 此过滤器的名称应该被配置为 *envoy.filters.listener.http_inspector*。

示例
-----

过滤器配置的示例如下：

.. code-block:: yaml

  listener_filters:
    - name: "envoy.filters.listener.http_inspector"
      typed_config: {}

统计
-----

此过滤器有一个以 *http_inspector* 为根的统计数，且有如下统计信息：

.. csv-table::
  :header: 名称, 类型, 描述
  :widths: 1, 1, 2

  read_error, Counter, 读取错误总数
  http10_found, Counter, 发现 HTTP/1.0 的总次数
  http11_found, Counter, 发现 HTTP/1.1 的总次数
  http2_found, Counter, 发现 HTTP/1.2 的总次数
  http_not_found, Counter, 未发现 HTTP 协议的总次数
