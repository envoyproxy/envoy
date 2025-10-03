.. _config_trace_opentelemetry_propagators:

OpenTelemetry Propagators
========================

OpenTelemetry propagators handle trace context propagation across service boundaries.
Envoy's OpenTelemetry tracer supports multiple propagation formats simultaneously for
maximum interoperability in heterogeneous service meshes.

Configuration
-------------

Propagators are configured through the :ref:`propagators <envoy_v3_api_field_config.trace.v3.OpenTelemetryConfig.propagators>`
field in the OpenTelemetry tracer configuration or via the ``OTEL_PROPAGATORS`` environment variable.

Priority Order
~~~~~~~~~~~~~~

Configuration is resolved in the following priority order:

1. **Explicit Configuration**: The ``propagators`` field in Envoy configuration
2. **Environment Variable**: ``OTEL_PROPAGATORS`` (comma-separated list)
3. **Default**: ``["tracecontext"]`` for W3C Trace Context propagation

Behavior
~~~~~~~~

**Extraction (Inbound Requests):**
Propagators are tried in the configured order. The first propagator that successfully
extracts valid trace context from incoming headers is used. This enables graceful
fallback between different formats.

**Injection (Outbound Requests):**
All configured propagators inject their respective headers into outgoing requests,
maximizing compatibility with downstream services.

Supported Propagators
--------------------

tracecontext
~~~~~~~~~~~~

**Format**: W3C Trace Context
**Headers**: ``traceparent``, ``tracestate``
**Specification**: `W3C Trace Context <https://www.w3.org/TR/trace-context/>`_

The default and recommended propagator for OpenTelemetry-compliant services.
Provides standardized trace context propagation with optional vendor-specific
trace state information.

**Example Headers:**

.. code-block:: text

   traceparent: 00-4bf92f3577b34da6a3ce929d0e0e4736-00f067aa0ba902b7-01
   tracestate: rojo=00f067aa0ba902b7,congo=t61rcWkgMzE

baggage
~~~~~~~

**Format**: W3C Baggage
**Headers**: ``baggage``
**Specification**: `W3C Baggage <https://www.w3.org/TR/baggage/>`_

Enables cross-service metadata propagation throughout the request lifecycle.
Baggage data is available to applications during request processing without
requiring out-of-band communication.

**Example Header:**

.. code-block:: text

   baggage: userId=alice,serverNode=DF:28,isProduction=false

b3
~~

**Format**: Zipkin B3
**Headers**: ``b3`` (single) or ``X-B3-*`` (multi)
**Specification**: `B3 Propagation <https://github.com/openzipkin/b3-propagation>`_

Provides compatibility with Zipkin ecosystems. Automatically detects and supports
both single-header and multi-header B3 formats.

**Single-Header Example:**

.. code-block:: text

   b3: 4bf92f3577b34da6a3ce929d0e0e4736-00f067aa0ba902b7-1-00f067aa0ba902b6

**Multi-Header Example:**

.. code-block:: text

   X-B3-TraceId: 4bf92f3577b34da6a3ce929d0e0e4736
   X-B3-SpanId: 00f067aa0ba902b7
   X-B3-Sampled: 1
   X-B3-ParentSpanId: 00f067aa0ba902b6

Configuration Examples
----------------------

Default Configuration
~~~~~~~~~~~~~~~~~~~~~

When no propagators are explicitly configured, Envoy defaults to W3C Trace Context:

.. code-block:: yaml

   tracing:
     http:
       name: envoy.tracers.opentelemetry
       typed_config:
         "@type": type.googleapis.com/envoy.config.trace.v3.OpenTelemetryConfig
         service_name: my-service
         # No propagators specified - defaults to ["tracecontext"]

Single Propagator
~~~~~~~~~~~~~~~~

Explicit W3C Trace Context configuration:

.. code-block:: yaml

   tracing:
     http:
       name: envoy.tracers.opentelemetry
       typed_config:
         "@type": type.googleapis.com/envoy.config.trace.v3.OpenTelemetryConfig
         service_name: my-service
         propagators:
           - tracecontext

Multiple Propagators
~~~~~~~~~~~~~~~~~~~

Full interoperability setup supporting W3C and Zipkin formats:

.. code-block:: yaml

   tracing:
     http:
       name: envoy.tracers.opentelemetry
       typed_config:
         "@type": type.googleapis.com/envoy.config.trace.v3.OpenTelemetryConfig
         service_name: my-service
         propagators:
           - tracecontext  # Primary format
           - baggage       # Cross-service metadata
           - b3           # Zipkin compatibility

Environment Variable Configuration
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Using environment variables instead of explicit configuration:

.. code-block:: bash

   export OTEL_PROPAGATORS=tracecontext,baggage,b3

.. code-block:: yaml

   tracing:
     http:
       name: envoy.tracers.opentelemetry
       typed_config:
         "@type": type.googleapis.com/envoy.config.trace.v3.OpenTelemetryConfig
         service_name: my-service
         # propagators read from OTEL_PROPAGATORS environment variable

Migration Guide
---------------

Upgrading from Single-Format Propagation
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Existing OpenTelemetry configurations continue to work without changes:

**Before (implicit default):**

.. code-block:: yaml

   tracing:
     http:
       name: envoy.tracers.opentelemetry
       typed_config:
         "@type": type.googleapis.com/envoy.config.trace.v3.OpenTelemetryConfig
         service_name: my-service

**After (explicit multi-format):**

.. code-block:: yaml

   tracing:
     http:
       name: envoy.tracers.opentelemetry
       typed_config:
         "@type": type.googleapis.com/envoy.config.trace.v3.OpenTelemetryConfig
         service_name: my-service
         propagators:
           - tracecontext
           - b3  # Added for legacy service compatibility

Rolling Deployment Strategy
~~~~~~~~~~~~~~~~~~~~~~~~~~

1. **Phase 1**: Add propagators configuration with tracecontext first:

   .. code-block:: yaml

      propagators: ["tracecontext", "b3"]

2. **Phase 2**: Deploy across all services with monitoring
3. **Phase 3**: Reorder propagators if needed based on service mesh composition

Troubleshooting
---------------

Common Issues
~~~~~~~~~~~~

**Trace Context Not Propagating:**

1. Verify propagator order matches your service mesh's primary format
2. Check that upstream services inject expected headers
3. Ensure downstream services handle multiple header formats

**Performance Concerns:**

- Multiple propagators add minimal overhead during header processing
- Consider limiting to 2-3 propagators for optimal performance
- Place the most common format first in the propagators list

**Environment Variable Not Working:**

1. Verify ``OTEL_PROPAGATORS`` is set in Envoy's environment
2. Check that explicit ``propagators`` field is not overriding the environment variable
3. Ensure environment variable format is comma-separated without spaces

Debugging
~~~~~~~~~

Enable debug logging to inspect propagator behavior:

.. code-block:: yaml

   admin:
     address:
       socket_address:
         address: 127.0.0.1
         port_value: 9901

Use the admin interface to inspect trace headers:

.. code-block:: bash

   curl -s "http://localhost:9901/stats?filter=tracing"

Best Practices
--------------

Propagator Selection
~~~~~~~~~~~~~~~~~~~

- **Greenfield deployments**: Use ``["tracecontext"]`` for simplicity
- **Mixed environments**: Use ``["tracecontext", "b3"]`` for broad compatibility
- **Baggage requirements**: Add ``baggage`` to support cross-service metadata
- **Performance-critical**: Limit to essential propagators only

Order Optimization
~~~~~~~~~~~~~~~~~

- Place your service mesh's primary format first
- Order by expected frequency of incoming requests
- Consider upstream service capabilities

Monitoring
~~~~~~~~~

Monitor propagator effectiveness:

- Track successful trace extraction rates
- Monitor header size impact on requests
- Verify trace continuity across service boundaries

See Also
--------

- :ref:`OpenTelemetry Configuration <envoy_v3_api_msg_config.trace.v3.OpenTelemetryConfig>`
- :ref:`Tracing Architecture <arch_overview_tracing>`
- :ref:`HTTP Headers <config_http_conn_man_headers>`