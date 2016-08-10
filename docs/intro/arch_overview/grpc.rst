.. _arch_overview_grpc:

GRPC
====

`GRPC <http://www.grpc.io/>`_ is a new RPC framework from Google. It uses protocol buffers as the
underlying serialization/IDL format. At the transport layer it uses HTTP/2 for request/response
multiplexing. Envoy has first class support for GRPC both at the transport layer as well as at the
application layer:

* GRPC makes use of HTTP/2 trailers for indicating ultimate RPC success. Envoy is one of very few
  HTTP proxies that correctly supports HTTP/2 trailers and is thus one of the few proxies that can
  transport GRPC requests and responses.
* The GRPC runtime for some languages is relatively immature. Envoy supports a GRPC :ref:`bridge
  filter <config_http_filters_grpc_bridge>` that allows GRPC requests to be sent to Envoy over
  HTTP/1.1. Envoy then translates the requests to HTTP/2 for transport to the target server.
  The response is translated back to HTTP/1.1.
* When installed, the bridge filter gathers per RPC statistics in addition to the standard array
  of global HTTP statistics.
