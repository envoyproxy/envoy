.. _config_http_filters_buffer:

Buffer
======

The buffer filter is used to stop filter iteration and wait for a fully buffered complete request.
This is useful in different situations including protecting some applications from having to deal
with partial requests and high network latency.

.. code-block:: json

  {
    "type": "decoder",
    "name": "buffer",
    "config": {
      "max_request_bytes": "...",
      "max_request_time_s": "..."
    }
  }

max_request_bytes
  *(required, integer)* The maximum request size that the filter will before before it stops
  buffering and returns a 413 response.

max_request_time_s
  *(required, integer)* The maximum amount of time that the filter will wait for a complete request
  before returning a 408 response.

Statistics
----------

The buffer filter outputs statistics in the *http.<stat_prefix>.buffer.* namespace. The :ref:`stat
prefix <config_http_conn_man_stat_prefix>` comes from the owning HTTP connection manager.

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  rq_timeout, Counter, Description
  rq_too_large, Counter, Description
