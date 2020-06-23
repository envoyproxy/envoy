.. _arch_overview_grpc:

gRPC
====

`gRPC <https://www.grpc.io/>`_ is an RPC framework from Google. It uses protocol buffers as the
underlying serialization/IDL format. At the transport layer it uses HTTP/2 for request/response
multiplexing. Envoy has first class support for gRPC both at the transport layer as well as at the
application layer:

* gRPC makes use of HTTP/2 trailers to convey request status. Envoy is one of very few HTTP proxies
  that correctly supports HTTP/2 trailers and is thus one of the few proxies that can transport
  gRPC requests and responses.
* The gRPC runtime for some languages is relatively immature. See :ref:`below <arch_overview_grpc_bridging>`
  for an overview of filters that can help bring gRPC to more languages.
* gRPC-Web is supported by a :ref:`filter <config_http_filters_grpc_web>` that allows a gRPC-Web
  client to send requests to Envoy over HTTP/1.1 and get proxied to a gRPC server. It's under
  active development and is expected to be the successor to the gRPC :ref:`bridge filter
  <config_http_filters_grpc_bridge>`.
* gRPC-JSON transcoder is supported by a :ref:`filter <config_http_filters_grpc_json_transcoder>`
  that allows a RESTful JSON API client to send requests to Envoy over HTTP and get proxied to a
  gRPC service.

.. _arch_overview_grpc_bridging:

gRPC bridging
-------------

Envoy supports two gRPC bridges:

* :ref:`grpc_http1_bridge filter <config_http_filters_grpc_bridge>` which allows gRPC requests to be sent to Envoy over
  HTTP/1.1. Envoy then translates the requests to HTTP/2 for transport to the target server. The response is translated back to HTTP/1.1.
  When installed, the bridge filter gathers per RPC statistics in addition to the standard array of global HTTP statistics.
* :ref:`grpc_http1_reverse_bridge filter <config_http_filters_grpc_http1_reverse_bridge>` which allows gRPC requests to be sent to Envoy
  and then translated to HTTP/1.1 when sent to the upstream. The response is then converted back into gRPC when sent to the downstream.
  This filter can also optionally manage the gRPC frame header, allowing the upstream to not have to be gRPC aware at all.

.. _arch_overview_grpc_services:

gRPC services
-------------

In addition to proxying gRPC on the data plane, Envoy makes use of gRPC for its
control plane, where it :ref:`fetches configuration from management server(s)
<config_overview>` and in filters, such as for :ref:`rate limiting
<config_http_filters_rate_limit>` or authorization checks. We refer to these as
*gRPC services*.

When specifying gRPC services, it's necessary to specify the use of either the
:ref:`Envoy gRPC client <envoy_v3_api_field_config.core.v3.GrpcService.envoy_grpc>` or the
:ref:`Google C++ gRPC client <envoy_v3_api_field_config.core.v3.GrpcService.google_grpc>`. We
discuss the tradeoffs in this choice below.

The Envoy gRPC client is a minimal custom implementation of gRPC that makes use
of Envoy's HTTP/2 upstream connection management. Services are specified as
regular Envoy :ref:`clusters <arch_overview_cluster_manager>`, with regular
treatment of :ref:`timeouts, retries <arch_overview_http_conn_man>`, endpoint
:ref:`discovery <arch_overview_dynamic_config_eds>`/:ref:`load
balancing/failover <arch_overview_load_balancing>`/load reporting, :ref:`circuit
breaking <arch_overview_circuit_break>`, :ref:`health checks
<arch_overview_health_checking>`, :ref:`outlier detection
<arch_overview_outlier_detection>`. They share the same :ref:`connection pooling
<arch_overview_conn_pool>` mechanism as the Envoy data plane. Similarly, cluster
:ref:`statistics <arch_overview_statistics>` are available for gRPC services.
Since the client is minimal, it does not include advanced gRPC features such as
`OAuth2 <https://oauth.net/2/>`_ or `gRPC-LB
<https://grpc.io/blog/loadbalancing>`_ lookaside.

The Google C++ gRPC client is based on the reference implementation of gRPC
provided by Google at https://github.com/grpc/grpc. It provides advanced gRPC
features that are missing in the Envoy gRPC client. The Google C++ gRPC client
performs its own load balancing, retries, timeouts, endpoint management, etc,
independent of Envoy's cluster management. The Google C++ gRPC client also
supports `custom authentication plugins
<https://grpc.io/docs/guides/auth.html#extending-grpc-to-support-other-authentication-mechanisms>`_.

It is recommended to use the Envoy gRPC client in most cases, where the advanced
features in the Google C++ gRPC client are not required. This provides
configuration and monitoring simplicity. Where necessary features are missing
in the Envoy gRPC client, the Google C++ gRPC client should be used instead.
