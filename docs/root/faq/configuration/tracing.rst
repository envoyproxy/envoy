.. _common_configuration_tracing:

How do I configure tracing?
===========================

See the :ref:`architecture overview <arch_overview_tracing>`. In the addition there are
:ref:`sandboxes <start_sandboxes>` that demonstrate several different tracing providers.

How do I configure the Zipkin collector with custom headers and full URIs?
=========================================================================

The Zipkin tracer supports advanced configuration using the ``collector_service`` field with HttpService.
This allows you to add custom HTTP headers to collector requests and use full URIs with automatic
hostname and path extraction.

**Full URI Support with Automatic Parsing:**

The ``uri`` field supports both path-only and full URI formats:

.. code-block:: yaml

  tracing:
    provider:
      name: envoy.tracers.zipkin
      typed_config:
        "@type": type.googleapis.com/envoy.config.trace.v3.ZipkinConfig
        collector_service:
          http_uri:
            # Full URI format - hostname and path are extracted automatically
            uri: "https://zipkin-collector.example.com/api/v2/spans"
            cluster: zipkin
            timeout: 5s
          request_headers_to_add:
            - header:
                key: "X-Custom-Token"
                value: "your-custom-token"
            - header:
                key: "X-Service-ID"  
                value: "your-service-id"

**URI Parsing Behavior:**

* **Full URI**: ``"https://zipkin-collector.example.com/api/v2/spans"``
  
  * **Hostname**: ``zipkin-collector.example.com`` (sets HTTP ``Host`` header)
  * **Path**: ``/api/v2/spans`` (sets HTTP request path)

* **Path only**: ``"/api/v2/spans"`` 
  
  * **Hostname**: Uses cluster name as fallback
  * **Path**: ``/api/v2/spans``

**Legacy Configuration (Deprecated):**

The legacy configuration fields (``collector_cluster``, ``collector_endpoint``, ``collector_hostname``) 
will be deprecated in a future release. These fields do not support custom headers or URI parsing. 
When both configurations are present, ``collector_service`` takes precedence.

**Migration Recommendation:**

Users should migrate to ``collector_service`` configuration to take advantage of custom headers 
and URI parsing capabilities, and to prepare for the future deprecation of legacy fields.

This feature is different from custom tags, which add metadata to individual spans within the trace data.
Custom headers are added to the HTTP requests sent to the Zipkin backend for purposes like service identification.
