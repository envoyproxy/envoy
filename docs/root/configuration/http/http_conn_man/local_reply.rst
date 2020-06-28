.. _config_http_conn_man_local_reply:

Local reply modification
========================

The :ref:`HTTP connection manager <arch_overview_http_conn_man>` supports modification of local reply which is response returned by Envoy itself.

Features:

* :ref:`Local reply content modification<config_http_conn_man_local_reply_modification>`.
* :ref:`Local reply format modification<config_http_conn_man_local_reply_format>`.

.. _config_http_conn_man_local_reply_modification:

Local reply content modification
--------------------------------

The local response content returned by Envoy can be customized. A list of :ref:`mappers <envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.LocalReplyConfig.mappers>` can be specified. Each mapper must have a :ref:`filter <envoy_v3_api_field_config.accesslog.v3.AccessLog.filter>`. It may have following rewrite rules; a :ref:`status_code <envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.ResponseMapper.status_code>` rule to rewrite response code, a :ref:`body <envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.ResponseMapper.body>` rule to rewrite the local reply body and a :ref:`body_format_override <envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.ResponseMapper.body_format_override>` to specify the response body format. Envoy checks each `mapper` according to the specified order until the first one is matched. If a `mapper` is matched, all its rewrite rules will apply.

Example of a LocalReplyConfig

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
    body:
      inline_string: "not allowed"

In above example, if the status_code is 400,  it will be rewritten to 401, the response body will be rewritten to as "not allowed".

.. _config_http_conn_man_local_reply_format:

Local reply format modification
-------------------------------

The response body content type can be customized. If not specified, the content type is plain/text. There are two `body_format` fields; one is the :ref:`body_format <envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.LocalReplyConfig.body_format>` field in the :ref:`LocalReplyConfig <envoy_v3_api_msg_extensions.filters.network.http_connection_manager.v3.LocalReplyConfig>` message and the other :ref:`body_format_override <envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.ResponseMapper.body_format_override>` field in the `mapper`. The latter is only used when its mapper is matched. The former is used if there is no any matched mappers, or the matched mapper doesn't have the `body_format` specified.

Local reply format can be specified as :ref:`SubstitutionFormatString <envoy_v3_api_msg_config.core.v3.SubstitutionFormatString>`. It supports :ref:`text_format <envoy_v3_api_field_config.core.v3.SubstitutionFormatString.text_format>` and :ref:`json_format <envoy_v3_api_field_config.core.v3.SubstitutionFormatString.json_format>`.

Example of a LocalReplyConfig with `body_format` field.

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
      text_format: "%LOCAL_REPLY_BODY% %REQ(:path)%"
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

In above example, there is a `body_format_override` inside the first `mapper` with a filter matching `status_code == 400`. It generates the response body in plain text format by concatenating %LOCAL_REPLY_BODY% with the `:path` request header. It is only used when the first mapper is matched. There is a `body_format` at the bottom of the config and at the same level as field `mappers`. It is used when non of the mappers is matched or the matched mapper doesn't have its own `body_format_override` specified.
