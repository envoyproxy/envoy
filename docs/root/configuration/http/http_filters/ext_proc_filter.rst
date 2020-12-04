.. _config_http_filters_ext_proc:

External Processing
===================
* :ref:`Http filter v3 API reference <envoy_v3_api_msg_extensions.filters.http.ext_proc.v3alpha.ExternalProcessor>`
* This filter should be configured with the name *envoy.filters.http.ext_proc*

The external processing filter calls an external gRPC service to enable it to participate in
HTTP filter chain processing. The filter is called using a gRPC bidirectional stream, and allows
the filter to make decisions in real time about what parts of the HTTP request / response stream
are sent to the filter for processing.

The protocol itself is based on a bidirectional gRPC stream. Envoy will send the
server 
:ref:`ProcessingRequest <envoy_v3_api_msg_service.ext_proc.v3alpha.ProcessingRequest>`
messages, and the server must reply with 
:ref:`ProcessingResponse <envoy_v3_api_msg_service.ext_proc.v3alpha.ProcessingResponse>`.

This filter is a work in progress. In its current state, it actually does nothing.
