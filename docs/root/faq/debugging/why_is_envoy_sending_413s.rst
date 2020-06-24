.. _faq_why_is_envoy_sending_413:

Why is Envoy sending 413s?
==========================

Envoy by default imposes limits to how much it will buffer for a given request. Generally, Envoy filters are designed to be streaming, and will pass data from downstream to upstream, or will simply pause processing while waiting for an external event (e.g. doing auth checks). Some filters, for example the buffer filter, require buffering the full request or response. If a request body is too large to buffer, but buffering is required by the filter, Envoy will send a 413. The buffer limits can be increased at the risk of making OOMs more possible. Please see the ref:`flow control docs <faq_flow_control>` for details.
