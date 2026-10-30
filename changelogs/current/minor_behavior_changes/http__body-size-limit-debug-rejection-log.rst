The :ref:`body size limit filter <config_http_filters_body_size_limit>` logs a rejected request at
``debug`` level per stream instead of at ``error``, so a client sending oversized requests can no
longer drive the error log. The new rejection counters and the
``request_payload_too_large`` response code detail record each rejection.
