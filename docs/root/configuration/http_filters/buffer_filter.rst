.. _config_http_filters_buffer:

Buffer
======

The buffer filter is used to stop filter iteration and wait for a fully buffered complete request.
This is useful in different situations including protecting some applications from having to deal
with partial requests and high network latency.

* :ref:`v1 API reference <config_http_filters_buffer_v1>`
* :ref:`v2 API reference <envoy_api_msg_config.filter.http.buffer.v2.Buffer>`

Statistics
----------

The buffer filter outputs statistics in the *http.<stat_prefix>.buffer.* namespace. The :ref:`stat
prefix <config_http_conn_man_stat_prefix>` comes from the owning HTTP connection manager.

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  rq_timeout, Counter, Total requests that timed out waiting for a full request
