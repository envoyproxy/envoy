.. _arch_overview_access_log_filters:

Access log filters
==================

Envoy supports several built-in access log filters. Currently supported filters are:

* Status code filter - filters on HTTP response/status code.
* Duration filter - filters on total request duration in milliseconds.
* Not health check filter - filters for requests that are not health check requests.
* Traceable filter - filters for requests that are traceable.
* Runtime filter - filters for random sampling of requests.
* And filter - performs a logical “and” operation on the result of each filter in filters.
  Filters are evaluated sequentially and if one of them returns false, the filter returns false
  immediately.
* Or filter - performs a logical “or” operation on the result of each individual filter.
  Filters are evaluated sequentially and if one of them returns true, the filter returns true
  immediately.
* Header filter - filters requests based on the presence or value of a request header.
* Response flag filter - filters requests that received responses with an Envoy response flag set.
* gRPC status filter - filters gRPC requests based on their response status.
* Extension filter - custom filter defined and registered at runtime. Filter configuration is passed
  to the class that implements a method that returns true if request should be logged.


Further reading
---------------

* Access log :ref:`configuration <config_access_log>`.
* Filter :ref:`configuration <envoy_api_field_config.filter.accesslog.v2.AccessLogFilter>`.
