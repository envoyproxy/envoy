.. _common_configuration_tracing:

How do I configure tracing?
===========================

See the :ref:`architecture overview <arch_overview_tracing>`. In the addition there are
:ref:`sandboxes <start_sandboxes>` that demonstrate several different tracing providers.

How do I add custom headers to Zipkin collector requests?
==========================================================

The Zipkin tracer supports adding custom HTTP headers to requests sent to the Zipkin collector
using the ``collector_request_headers`` configuration option. This is useful for authentication,
authorization, or collector-specific routing.

.. code-block:: yaml

  tracing:
    provider:
      name: envoy.tracers.zipkin
      typed_config:
        "@type": type.googleapis.com/envoy.config.trace.v3.ZipkinConfig
        collector_cluster: zipkin
        collector_endpoint: "/api/v2/spans"
        collector_endpoint_version: HTTP_JSON
        collector_request_headers:
          - key: "Authorization"
            value: "Bearer your-auth-token"
          - key: "X-API-Key"
            value: "your-api-key"

This feature is different from custom tags, which add metadata to individual spans within the trace data.
Custom headers are added to the HTTP requests sent to the Zipkin backend for purposes like authentication.
