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

The local response content returned by Envoy can be customized. A list of :ref:`mappers <envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.LocalReplyConfig.mappers>` can be specified. Each mapper must have a :ref:`filter <envoy_v3_api_field_config.accesslog.v3.AccessLog.filter>`, a :ref:`rewriter <envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.ResponseMapper.rewriter>` rule and optional :ref:`format <envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.ResponseMapper.format>`. Envoy checks each `mapper` according to the specified order until the first one is matched. If a `mapper` is matched, its `rewriter` rule will apply. Each `rewriter` rule supports rewritting status_code and response body. If a matched mapper has the `format` field specified, the `format` will apply too.

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
      rewriter:
         status_code: 401
	 body:
	   inline_string: "not allowed"

In above example, if the response_code is 400,  it will be rewritten to 401, the error messsage will be rewritten to "not allowed".

.. _config_http_conn_man_local_reply_format:

Local reply format modification
-------------------------------

The response body content type can be customized. If no specified, the content type is plain/text. There are two `format` fields; one is the :ref:`format <envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.LocalReplyConfig.format>` field in the :ref:`LocalReplyConfig <envoy_v3_api_msg_extensions.filters.network.http_connection_manager.v3.LocalReplyConfig>` message and the other :ref:`format <envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.ResponseMapper.format>` field in the `mapper`. The latter is only used when its mapper is matched. The former is used if there is no any matched mappers, or the matched mapper doesn't have the `format` specified.

Local reply format can be specified as :ref:`SubstitutionFormatString <envoy_v3_api_msg_config.core.v3.SubstitutionFormatString>`. It supports :ref:`text_format <envoy_v3_api_field_config.core.v3.SubstitutionFormatString.text_format>` and :ref:`json_format <envoy_v3_api_field_config.core.v3.SubstitutionFormatString.json_format>`.
