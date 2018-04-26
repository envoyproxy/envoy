.. _arch_overview_access_logs:

Access logging
===================

The :ref:`HTTP connection manager <arch_overview_http_conn_man>` and
:ref:`tcp proxy <arch_overview_tcp_proxy>` supports extensible access logging with the following
features:

* Any number of access logs per connection manager or tcp proxy.
* Asynchronous IO flushing architecture. Access logging will never block the main network processing
  threads.
* Customizable access log formats using predefined fields as well as arbitrary HTTP request and
  response headers.
* Customizable access log filters that allow different types of requests and responses to be written
  to different access logs.

Access log :ref:`configuration <config_access_log>`.

