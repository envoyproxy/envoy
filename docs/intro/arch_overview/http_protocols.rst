.. _arch_overview_http_protocols:

HTTP protocols
==============

Envoy’s HTTP connection manager has native support for both HTTP/1.1 and HTTP/2. It does not support
SPDY. Envoy’s HTTP support was designed to first and foremost be an HTTP/2 multiplexing proxy.
Internally, HTTP/2 terminology is used to describe system components. For example, an HTTP request
and response take place on a *stream*. A codec API is used to translate from different wire
protocols into a protocol agnostic form for streams, requests, responses, etc. In the case of
HTTP/1.1, the codec translates the serial/pipelining capabilities of the protocol into something
that looks like HTTP/2 to higher layers. This means that the majority of the code does not need to
understand whether a stream originated on an HTTP/1.1 or HTTP/2 connection.
