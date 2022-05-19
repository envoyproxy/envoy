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
  x-request-id header for unified logging as well as tracing. The behavior can be configured on a
  per :ref:`HTTP connection manager<envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.request_id_extension>`
  basis using an extension.
* **Client trace ID joining**: The :ref:`config_http_conn_man_headers_x-client-trace-id` header can
  be used to join untrusted request IDs to the trusted internal
  :ref:`config_http_conn_man_headers_x-request-id`.
* **External trace service integration**: Envoy supports pluggable external trace visualization
  providers, that are divided into two subgroups:

  - External tracers which are part of the Envoy code base, like `Zipkin <https://zipkin.io/>`_,
    `Jaeger <https://github.com/jaegertracing/>`_,
    `Datadog <https://datadoghq.com>`_, `SkyWalking <http://skywalking.apache.org/>`_, and
    `AWS X-Ray <https://docs.aws.amazon.com/xray/latest/devguide/xray-gettingstarted.html>`_.
  - External tracers which come as a third party plugin, like `Instana <https://www.instana.com/blog/monitoring-envoy-proxy-microservices/>`_.

How to initiate a trace
-----------------------
The HTTP connection manager that handles the request must have the :ref:`tracing
<envoy_v3_api_msg_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.Tracing>` object set. There are several ways tracing can be
initiated:

* By an external client via the :ref:`config_http_conn_man_headers_x-client-trace-id`
  header.
* By an internal service via the :ref:`config_http_conn_man_headers_x-envoy-force-trace`
  header.
* Randomly sampled via the :ref:`random_sampling <config_http_conn_man_runtime_random_sampling>`
  runtime setting.

The router filter is also capable of creating a child span for egress calls via the
:ref:`start_child_span <envoy_v3_api_field_extensions.filters.http.router.v3.Router.start_child_span>` option.

.. _arch_overview_tracing_context_propagation:

Trace context propagation
-------------------------
Envoy provides the capability for reporting tracing information regarding communications between
services in the mesh. However, to be able to correlate the pieces of tracing information generated
by the various proxies within a call flow, the services must propagate certain trace context between
the inbound and outbound requests.

Whichever tracing provider is being used, the service should propagate the
:ref:`config_http_conn_man_headers_x-request-id` to enable logging across the invoked services
to be correlated.

.. attention::

  Envoy's request ID implementation is extensible and defaults to the
  :ref:`UuidRequestIdConfig <envoy_v3_api_msg_extensions.request_id.uuid.v3.UuidRequestIdConfig>`
  implementation. Configuration for this extension can be provided within the
  :ref:`HTTP connection manager<envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.request_id_extension>`
  field (see the documentation for that field for an example). The default implementation will
  modify the request ID UUID4 to pack the final trace reason into the UUID. This feature allows
  stable sampling across a fleet of Envoys as documented in the :ref:`x-request-id <config_http_conn_man_headers_x-request-id>`
  header documentation. However, trace reason packing my break externally generated request IDs
  that must be maintained. The :ref:`pack_trace_reason <envoy_v3_api_field_extensions.request_id.uuid.v3.UuidRequestIdConfig.pack_trace_reason>`
  field can be used to disable this behavior at the expense of also disabling stable trace reason
  propagation and associated features within a deployment.

.. attention::

  The sampling policy for Envoy is determined by the value of :ref:`x-request-id <config_http_conn_man_headers_x-request-id>` by default.
  However, such a sampling policy is only valid for a fleet of Envoys. If a service proxy
  that is not Envoy is present in the fleet, sampling is performed without considering the policy of that proxy.
  For meshes consisting of multiple service proxies such as this, it is more effective to
  bypass Envoy's sampling policy and sample based on the trace provider's sampling policy. This can be achieved by setting
  :ref:`use_request_id_for_trace_sampling <envoy_v3_api_field_extensions.request_id.uuid.v3.UuidRequestIdConfig.use_request_id_for_trace_sampling>`
  to false.

The tracing providers also require additional context, to enable the parent/child relationships
between the spans (logical units of work) to be understood. This can be achieved by using the
LightStep (via OpenTracing API) or Zipkin tracer directly within the service itself, to extract the
trace context from the inbound request and inject it into any subsequent outbound requests. This
approach would also enable the service to create additional spans, describing work being done
internally within the service, that may be useful when examining the end-to-end trace.

Alternatively the trace context can be manually propagated by the service:

* When using the LightStep tracer, Envoy relies on the service to propagate the
  :ref:`config_http_conn_man_headers_x-ot-span-context` HTTP header
  while sending HTTP requests to other services.

* When using the Zipkin tracer, Envoy relies on the service to propagate the
  B3 HTTP headers (
  :ref:`config_http_conn_man_headers_x-b3-traceid`,
  :ref:`config_http_conn_man_headers_x-b3-spanid`,
  :ref:`config_http_conn_man_headers_x-b3-parentspanid`,
  :ref:`config_http_conn_man_headers_x-b3-sampled`, and
  :ref:`config_http_conn_man_headers_x-b3-flags`). The :ref:`config_http_conn_man_headers_x-b3-sampled`
  header can also be supplied by an external client to either enable or disable tracing for a particular
  request. In addition, the single :ref:`config_http_conn_man_headers_b3` header propagation format is
  supported, which is a more compressed format.

* When using the Datadog tracer, Envoy relies on the service to propagate the
  Datadog-specific HTTP headers (
  :ref:`config_http_conn_man_headers_x-datadog-trace-id`,
  :ref:`config_http_conn_man_headers_x-datadog-parent-id`,
  :ref:`config_http_conn_man_headers_x-datadog-sampling-priority`).

* When using the SkyWalking tracer, Envoy relies on the service to propagate the
  SkyWalking-specific HTTP headers (
  :ref:`config_http_conn_man_headers_sw8`).

* When using the AWS X-Ray tracer, Envoy relies on the service to propagate the
  X-Ray-specific HTTP headers (
  :ref:`config_http_conn_man_headers_x-amzn-trace-id`).

What data each trace contains
-----------------------------
An end-to-end trace is comprised of one or more spans. A
span represents a logical unit of work that has a start time and duration and can contain metadata
associated with it. Each span generated by Envoy contains the following data:

* Originating service cluster set via :option:`--service-cluster`.
* Start time and duration of the request.
* Originating host set via :option:`--service-node`.
* Downstream cluster set via the :ref:`config_http_conn_man_headers_downstream-service-cluster`
  header.
* HTTP request URL, method, protocol and user-agent.
* Additional custom tags set via :ref:`custom_tags
  <envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.Tracing.custom_tags>`.
* Upstream cluster name, observability name, and address.
* HTTP response status code.
* GRPC response status and message (if available).
* An error tag when HTTP status is 5xx or GRPC status is not "OK" and represents a server side error.
  See `GRPC's documentation <https://grpc.github.io/grpc/core/md_doc_statuscodes.html>`_ for more information about GRPC status code.
* Tracing system-specific metadata.

The span also includes a name (or operation) which by default is defined as the host of the invoked
service. However this can be customized using a :ref:`envoy_v3_api_msg_config.route.v3.Decorator` on
the route. The name can also be overridden using the
:ref:`config_http_filters_router_x-envoy-decorator-operation` header.

Envoy automatically sends spans to tracing collectors. Depending on the tracing collector,
multiple spans are stitched together using common information such as the globally unique
request ID :ref:`config_http_conn_man_headers_x-request-id` (LightStep) or
the trace ID configuration (Zipkin and Datadog). See
:ref:`v3 API reference <envoy_v3_api_msg_config.trace.v3.Tracing>`
for more information on how to setup tracing in Envoy.

Baggage
-----------------------------
Baggage provides a mechanism for data to be available throughout the entirety of a trace.
While metadata such as tags are usually communicated to collectors out-of-band, baggage data is injected into the actual
request context and available to applications during the duration of the request. This enables metadata to transparently
travel from the beginning of the request throughout your entire mesh without relying on application-specific modifications for
propagation. See `OpenTracing's documentation <https://opentracing.io/docs/overview/tags-logs-baggage/>`_ for more information about baggage.

Tracing providers have varying level of support for getting and setting baggage:

* Lightstep (and any OpenTracing-compliant tracer) can read/write baggage
* Zipkin support is not yet implemented
* X-Ray and OpenCensus don't support baggage
