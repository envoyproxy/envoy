Changed the log level from ``error`` to ``debug`` for the message the
:ref:`body size limit filter <config_http_filters_body_size_limit>` logs when it rejects a request
for exceeding the configured limit. Downstream clients trigger this message, so logging it at
``error`` level could cause excessive logging.
