Added :ref:`%REQ_ALL_HEADERS% and %RESP_ALL_HEADERS% <config_advanced_substitution_formatter>`
access log formatters that serialize all HTTP request or response headers as a JSON object.
Configured via the ``envoy.formatter.all_headers`` extension with ``exclude_headers`` and
``max_value_bytes`` options.
