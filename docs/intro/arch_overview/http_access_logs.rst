.. _arch_overview_http_access_logs:

HTTP access logging
===================

The :ref:`HTTP connection manager <arch_overview_http_conn_man>` supports extensible access
logging with the following features:

* Any number of access logs per connection manager.
* Asynchronous IO flushing architecture. Access logging will never block the main network processing
  threads.
* Customizable access log formats using predefined fields as well as arbitrary HTTP request and
  response headers.
* Customizable access log filters that allow different types of requests and responses to be written
  to different access logs.

HTTP access log :ref:`configuration <config_http_conn_man_access_log>`.
