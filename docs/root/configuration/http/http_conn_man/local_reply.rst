.. _config_http_conn_man_local_reply:

Local reply modification
========================

The :ref:`HTTP connection manager <arch_overview_http_conn_man>` supports modification of local reply which is response returned by Envoy itself
rather than response from cluster. 

Features:

* :ref:`Local reply content modification<config_http_conn_man_local_reply_modification>`.
* :ref:`Local reply format modification<config_http_conn_man_local_reply_format>`.

.. _config_http_conn_man_local_reply_modification:

Local reply content modification
--------------------------------

There is support for modification of local replies. You can specify list of :ref:`mappers <envoy_api_field_config.filter.network.http_connection_manager.v2.HttpConnectionManager.LocalReplyConfig.mapper>` which contains 
pairs of :ref:`filter <envoy_api_msg_config.filter.accesslog.v2.AccessLogFilter>` and :ref:`rewriter <envoy_api_msg_config.filter.network.http_connection_manager.v2.ResponseRewriter>`. Both elements in pair has to be
specified. If more than one pair is defined then first matching is used.

Example how to change status code when local reply contains any of these response flags:

.. code-block:: yaml

  mapper:
    filter:
      response_flag_filter:
        flags:
        - LH
        - UH
    rewriter:
      status_code: 504

.. _config_http_conn_man_local_reply_format:

Local reply format modification
-------------------------------

Local reply format contains command operators that extract the relevant data and insert it.
They support two formats: :ref:`format strings <config_http_conn_man_local_reply_format_string>` and 
:ref:`"format dictionaries" <config_http_conn_man_local_reply_dictionaries>`. In both cases, the :ref:`command operators <config_http_conn_man_local_reply_command_operators>`
are used to extract the relevant data, which is then inserted into the specified reply format.
Only one reply format may be specified at the time. 

.. _config_http_conn_man_local_reply_format_string:

Format Strings
--------------

Format strings are plain strings, specified using the ``format`` key. They may contain
either :ref:`command operators <config_http_conn_man_local_reply_command_operators>` or other characters interpreted as a plain string.
The access log formatter does not make any assumptions about a new line separator, so one
has to specified as part of the format string.

.. code-block:: none

  %RESP_BODY% %RESPONSE_CODE% %RESPONSE_FLAGS% "My custom response"

Example of custom Envoy local reply format:

.. code-block:: none

  upstream connect error or disconnect/reset before headers. reset reason: connection failure 204 UH My custom response


If format isn't specified then :ref:`default format <config_http_conn_man_local_reply_default_format>` is used.

.. _config_http_conn_man_local_reply_default_format:

Default Format String
---------------------

If custom format string is not specified, Envoy uses the following default format:

.. code-block:: none

  %RESP_BODY%

Example of the default local reply format:

.. code-block:: none

  upstream connect error or disconnect/reset before headers. reset reason: connection failure

.. _config_http_conn_man_local_reply_dictionaries:

Format Dictionaries
-------------------

Format dictionaries are dictionaries that specify a structured local reply output format,
specified using the ``json_format`` key. This allows response to be returned in a structured format
such as JSON.

More can be found in :ref:`configuration <config_access_log_format_dictionaries>`.

.. _config_http_conn_man_local_reply_command_operators:

Command Operators
-----------------

Local reply format reuse :ref:`access log operators <config_access_log_command_operators>`, so more information can be found there.
