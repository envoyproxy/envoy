.. _config_http_filters_buffer_v1:

Buffer
======

Buffer :ref:`configuration overview <config_http_filters_buffer>`.

.. code-block:: json

  {
    "name": "buffer",
    "config": {
      "max_request_bytes": "...",
      "max_request_time_s": "..."
    }
  }

max_request_bytes
  *(required, integer)* The maximum request size that the filter will buffer before the connection
  manager will stop buffering and return a 413 response.

max_request_time_s
  *(required, integer)* The maximum number of seconds that the filter will wait for a complete
  request before returning a 408 response.
