  .. _config_access_log:

Access logging
==============

Configuration
-------------------------

Access logs are configured as part of the :ref:`HTTP connection manager config
<config_http_conn_man>`, :ref:`TCP Proxy <config_network_filters_tcp_proxy>`,
:ref:`UDP Proxy <config_udp_listener_filters_udp_proxy>` or
:ref:`Thrift Proxy <config_network_filters_thrift_proxy>`.

* :ref:`v3 API reference <envoy_v3_api_msg_config.accesslog.v3.AccessLog>`

.. _config_access_log_format:

Format Rules
------------

Access log formats contain command operators that extract the relevant data and insert it.
They support two formats: :ref:`"format strings" <config_access_log_format_strings>` and
:ref:`"format dictionaries" <config_access_log_format_dictionaries>`. In both cases, the command operators
are used to extract the relevant data, which is then inserted into the specified log format.
Only one access log format may be specified at a time.

.. _config_access_log_format_strings:

Format Strings
--------------

Format strings are plain strings, specified using the ``format`` key. They may contain
either command operators or other characters interpreted as a plain string.
The access log formatter does not make any assumptions about a new line separator, so one
has to be specified as part of the format string.
See the :ref:`default format <config_access_log_default_format>` for an example.

.. _config_access_log_default_format:

Default Format String
---------------------

If a custom format string is not specified, Envoy uses the following default format:

.. code-block:: none

  [%START_TIME%] "%REQUEST_HEADER(:METHOD)% %REQUEST_HEADER(X-ENVOY-ORIGINAL-PATH?:PATH)% %PROTOCOL%"
  %RESPONSE_CODE% %RESPONSE_FLAGS% %BYTES_RECEIVED% %BYTES_SENT% %DURATION%
  %RESPONSE_HEADER(X-ENVOY-UPSTREAM-SERVICE-TIME)% "%REQUEST_HEADER(X-FORWARDED-FOR)%" "%REQUEST_HEADER(USER-AGENT)%"
  "%REQUEST_HEADER(X-REQUEST-ID)%" "%REQUEST_HEADER(:AUTHORITY)%" "%UPSTREAM_HOST%"

Example of the default Envoy access log format:

.. code-block:: console

  [2016-04-15T20:17:00.310Z] "POST /api/v1/locations HTTP/2" 204 - 154 0 226 100 "10.0.35.28"
  "nsq2http" "cc21d9b0-cf5c-432b-8c7e-98aeb7988cd2" "locations" "tcp://10.0.2.1:80"

.. _config_access_log_format_dictionaries:

Format Dictionaries
-------------------

Format dictionaries are dictionaries that specify a structured access log output format,
specified using the ``json_format`` or ``typed_json_format`` keys. This allows logs to be output in
a structured format such as JSON. Similar to format strings, command operators are evaluated and
their values inserted into the format dictionary to construct the log output.

For example, the following Envoy configuration snippet shows how to configure ``json_format``:

.. literalinclude:: _include/json-format-config.yaml
    :language: yaml
    :linenos:
    :lines: 13-25
    :emphasize-lines: 3-11
    :caption: :download:`json-format-config.yaml <_include/json-format-config.yaml>`

The following JSON object would be written to the log file:

.. code-block:: json

  {"protocol": "HTTP/1.1", "duration": "123", "my_custom_header": "value_of_MY_CUSTOM_HEADER"}

This allows you to specify a custom key for each command operator.

The ``typed_json_format`` differs from ``json_format`` in that values are rendered as JSON numbers,
booleans, and nested objects or lists where applicable. In the example, the request duration
would be rendered as the number ``123``.

Format dictionaries have the following restrictions:

* The dictionary must map strings to strings (specifically, strings to command operators). Nesting
  is supported.
* When using the ``typed_json_format`` command operators will only produce typed output if the
  command operator is the only string that appears in the dictionary value. For example,
  ``"%DURATION%"`` will log a numeric duration value, but ``"%DURATION%.0"`` will log a string
  value.

.. note::

  When using the ``typed_json_format``, integer values that exceed :math:`2^{53}` will be
  represented with reduced precision as they must be converted to floating point numbers.

.. _config_access_log_command_operators:

Command Operators
-----------------

Command operators are used to extract values that will be inserted into the access logs.
The same operators are used by different types of access logs (such as HTTP and TCP). Some
fields may have slightly different meanings, depending on what type of log it is. Differences
are noted in the descriptions.

.. note::

  If a value is not set/empty, the logs will contain a ``-`` character or, for JSON logs,
  the string ``"-"``. For typed JSON logs unset values are represented as ``null`` values and empty
  strings are rendered as ``""``. The :ref:`omit_empty_values
  <envoy_v3_api_field_config.core.v3.SubstitutionFormatString.omit_empty_values>` option could be used
  to omit empty values entirely.

Unless otherwise noted, command operators produce string outputs for typed JSON logs.

See all the available command operators in the :ref:`substitution formatter documentation
<config_advanced_substitution_operators>`.
