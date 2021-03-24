.. _config_http_conn_man_local_reply:

本地回复修改
=================

:ref:`HTTP 连接管理器 <arch_overview_http_conn_man>` 支持本地回复修改，这个回复是 Envoy 返回的响应。

功能：

* :ref:`本地回复内容修改 <config_http_conn_man_local_reply_modification>`。
* :ref:`本地回复格式修改 <config_http_conn_man_local_reply_format>`。

.. _config_http_conn_man_local_reply_modification:

本地回复内容修改
-----------------------

Envoy 返回的本地响应内容是可以定制的。可以指定 :ref:`映射器 <envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.LocalReplyConfig.mappers>` 列表。每个映射器必须具有一个 :ref:`过滤器 <envoy_v3_api_field_config.accesslog.v3.AccessLog.filter>`。它可能具有以下重写规则；用于重写响应代码的 :ref:`status_code <envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.ResponseMapper.status_code>` 规则，用于添加/覆盖/追加响应 HTTP 头部的 :ref:`headers_to_add <envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.ResponseMapper.headers_to_add>` 规则，用于重写本地回复正文的 :ref:`body <envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.ResponseMapper.body>` 规则以及用于指定响应正文格式的 :ref:`body_format_override <envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.ResponseMapper.body_format_override>` 规则。Envoy 根据指定的顺序检查每个`映射器`，直到第一个匹配。如果`映射器`匹配，则应用其所有重写规则。

LocalReplyConfig 示例

.. code-block::

  mappers:
  - filter:
      status_code_filter:
        comparison:
          op: EQ
          value:
            default_value: 400
            runtime_key: key_b
    headers_to_add:
      - header:
          key: "foo"
          value: "bar"
        append: false
    status_code: 401
    body:
      inline_string: "not allowed"

在上面的示例中，如果 status_code 为 400，它将被重写为 401，响应正文将被重写为 “not allowed”。

.. _config_http_conn_man_local_reply_format:

本地回复格式修改
-----------------------

响应主体内容类型可以自定义。如果未指定，则内容类型为 plain/text。有两个 `body_format` 字段；一个是 :ref:`body_format <envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.LocalReplyConfig.body_format>` 在 :ref:`LocalReplyConfig <envoy_v3_api_msg_extensions.filters.network.http_connection_manager.v3.LocalReplyConfig>` message 和另一个 :ref:`body_format_override <envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.ResponseMapper.body_format_override>` 字段在`映射器`中。后者仅在其映射器匹配时使用。如果没有匹配的映射器，或者匹配的映射器没有指定 body_format，则使用前者。

本地回复格式可以指定为 :ref:`SubstitutionFormatString <envoy_v3_api_msg_config.core.v3.SubstitutionFormatString>`。它支持 :ref:`text_format <envoy_v3_api_field_config.core.v3.SubstitutionFormatString.text_format>` 和 :ref:`json_format <envoy_v3_api_field_config.core.v3.SubstitutionFormatString.json_format>`。

以后也可以通过 :ref:`content_type <envoy_v3_api_field_config.core.v3.SubstitutionFormatString.content_type>` 字段修改 content-type。如果没有指定，:ref:`text_format <envoy_v3_api_field_config.core.v3.SubstitutionFormatString.text_format>` content-type 默认是 `text/plain`，:ref:`json_format <envoy_v3_api_field_config.core.v3.SubstitutionFormatString.json_format>` content-type 默认是 `application/json`。

带有 `body_format` 字段的 LocalReplyConfig 示例

.. code-block::

  mappers:
  - filter:
      status_code_filter:
        comparison:
          op: EQ
          value:
            default_value: 400
            runtime_key: key_b
    status_code: 401
    body_format_override:
      text_format: "<h1>%LOCAL_REPLY_BODY% %REQ(:path)%</h1>"
      content_type: "text/html; charset=UTF-8"
  - filter:
      status_code_filter:
        comparison:
          op: EQ
          value:
            default_value: 500
            runtime_key: key_b
    status_code: 501
  body_format:
    text_format: "%LOCAL_REPLY_BODY% %RESPONSE_CODE%"

在上面的示例中，第一个`映射器`中有一个 `body_format_override`，该`映射器`中的过滤器匹配 `status_code == 400`。通过将 %LOCAL_REPLY_BODY% 与 `:path` 请求头部连接在一起，它以纯文本格式生成响应主体。它仅在第一个映射器匹配时使用。在配置的底部有一个 `body_format`，与字段`映射器`处于同一级别。当不匹配任何映射器或匹配的映射器未指定自己的 `body_format_override` 时使用。
