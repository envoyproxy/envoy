.. _arch_overview_tracing:

Tracing
=======

Overview
--------
Distributed tracing allows developers to obtain visualizations of call flows in large service
oriented architectures. It can be invaluable in understanding serialization, parallelism, and
sources of latency. Envoy supports three features related to system wide tracing:

* **Request ID generation**: Envoy will generate UUIDs when needed and populate the
  :ref:`config_http_conn_man_headers_x-request-id` HTTP header. Applications can forward the
  x-request-id header for unified logging as well as tracing.
* **External trace service integration**: Envoy supports pluggable external trace visualization
  providers. Currently the only supported provider is for `LightStep <http://lightstep.com/>`_.
  However other providers for systems such as `Zipkin <http://zipkin.io/>`_ or `OpenTracing
  <http://opentracing.io/>`_ would not be difficult to add.
* **Client trace ID joining**: The :ref:`config_http_conn_man_headers_x-client-trace-id` header can
  be used to join untrusted request IDs to the trusted internal
  :ref:`config_http_conn_man_headers_x-request-id`.

How to initiate a trace
-----------------------
There are several ways trace can be initiated:

* The HTTP connection manager that handles the request must have the :ref:`tracing_enabled
  <config_http_conn_man_tracing_enabled>` option set.
* Initiated by an external client via the :ref:`config_http_conn_man_headers_x-client-trace-id`
  header.
* Initiated by an internal service via the :ref:`config_http_conn_man_headers_x-envoy-force-trace`
  header.
* Randomly sampled via the :ref:`random_sampling <config_http_conn_man_runtime_random_sampling>`
  runtime setting.

What data each trace contains
-----------------------------
Traces from every service are aggregated using the :ref:`config_http_conn_man_headers_x-request-id`
HTTP request header. Envoy automatically sends spans which compose a trace to tracing collectors. A
span represents a logical unit of work that has a start time and duration and can contain metadata
associated with it. Each Envoy span contains the following data:

* Originating service cluster set via :option:`--service-cluster`.
* Start time and duration of the request.
* Originating host set via :option:`--service-node`.
* Downstream cluster set via the :ref:`config_http_conn_man_headers_downstream-service-cluster`
  header.
* HTTP request line.
* HTTP response code.

Tracing :ref:`configuration <config_tracing>`.
