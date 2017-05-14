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

When using the LightStep tracer, Envoy relies on a service to propagate
:ref:`config_http_conn_man_headers_x-request-id` and
:ref:`config_http_conn_man_headers_x-ot-span-context` HTTP headers
while sending HTTP requests to other services. When using the Zipkin
tracer, Envoy relies on the service to
propagate :ref:`config_http_conn_man_headers_x-request-id`,
:ref:`config_http_conn_man_headers_x-ot-span-context`,
:ref:`config_http_conn_man_headers_x-b3-traceid`,
:ref:`config_http_conn_man_headers_x-b3-spanid`,
:ref:`config_http_conn_man_headers_x-b3-parentspanid`,
:ref:`config_http_conn_man_headers_x-b3-sampled`, and
:ref:`config_http_conn_man_headers_x-b3-flags` HTTP headers.

How to initiate a trace
-----------------------
The HTTP connection manager that handles the request must have the :ref:`tracing
<config_http_conn_man_tracing>` object set. There are several ways tracing can be
initiated:

* By an external client via the :ref:`config_http_conn_man_headers_x-client-trace-id`
  header.
* By an internal service via the :ref:`config_http_conn_man_headers_x-envoy-force-trace`
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
