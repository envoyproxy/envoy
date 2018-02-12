# Raw release notes

This file contains "raw" release notes for each release. The notes are added by developers as changes
are made to Envoy that are user impacting. When a release is cut, the releaser will use these notes
to populate the [Sphinx release notes in data-plane-api](https://github.com/envoyproxy/data-plane-api/blob/master/docs/root/intro/version_history.rst).
The idea is that this is a low friction way to add release notes along with code changes. Doing this
will make it substantially easier for the releaser to "linkify" all of the release notes in the
final version.

## 1.6.0
* Added support for inline delivery of TLS certificates and private keys.
* Added gRPC healthcheck based on [grpc.health.v1.Health](https://github.com/grpc/grpc/blob/master/src/proto/grpc/health/v1/health.proto) service.
* Added Metrics Service implementation.
* Added gRPC access logging.
* Added DOWNSTREAM_REMOTE_ADDRESS, DOWNSTREAM_REMOTE_ADDRESS_WITHOUT_PORT, and
  DOWNSTREAM_LOCAL_ADDRESS access log formatters. DOWNSTREAM_ADDRESS access log formatter has been
  deprecated.
* TCP proxy access logs now bring an IP address without a port when using DOWNSTREAM_ADDRESS.
  Use DOWNSTREAM_REMOTE_ADDRESS instead.
* Added DOWNSTREAM_REMOTE_ADDRESS_WITHOUT_PORT header formatter. CLIENT_IP header formatter has been
  deprecated.
* Added transport socket interface to allow custom implementation of transport socket. A transport socket
  provides read and write logic with buffer encryption and decryption. The existing TLS implementation is
  refactored with the interface.
* Added support for dynamic response header values (`%CLIENT_IP%` and `%PROTOCOL%`).
* Added native DogStatsD support. :ref:`DogStatsdSink <envoy_api_msg_DogStatsdSink>`
* grpc-json: Added support inline descriptor in config.
* Added support for listening for both IPv4 and IPv6 when binding to ::.
* Added support for :ref:`LocalityLbEndpoints<envoy_api_msg_LocalityLbEndpoints>` priorities.
* Added idle timeout to TCP proxy.
* Improved TCP proxy to correctly proxy TCP half-close.
* Added support for dynamic headers generated from upstream host endpoint metadata
  (`UPSTREAM_METADATA(...)`).
* Added restrictions for the backing sources of xDS resources. For filesystem based xDS the file
  must exist at configuration time. For cluster based xDS (api\_config\_source, and ADS) the backing
  cluster must be statically defined and be of non-EDS type.
* Added support for route matching based on URL query string parameters.
  :ref:`QueryParameterMatcher<envoy_api_msg_QueryParameterMatcher>`
* Added support for :ref:`fixed stats tag values
  <envoy_api_field_TagSpecifier.fixed_value>` which will be added to all metrics.
* Added `/runtime` admin endpoint to read the current runtime values.
* Extended the health check filter to support computation of the health check response
  based on the percent of healthy servers is upstream clusters.
* Added `gateway-error` retry-on policy.
* Added support for building envoy with exported symbols
  This change allows scripts loaded with the lua filter to load shared object libraries such as those installed via luarocks.
* The Google gRPC C++ library client is now supported as specified in the :ref:`gRPC services
  overview <arch_overview_grpc_services>` and :ref:`GrpcService <envoy_api_msg_GrpcService>`.
* Added cluster configuration for healthy panic threshold percentage.
* Added support for more granular weighted cluster routing by allowing the total weight to be specified in configuration.
* Added support for custom request/response headers with mixed static and dynamic values.
* Added support for [Squash microservices debugger](https://github.com/solo-io/squash).
  :ref:`Squash <envoy_api_msg_filter.http.Squash>` allows debugging an incoming request to a microservice in the mesh.
* lua: added headers replace() API.
* Added support for direct responses -- i.e., sending a preconfigured HTTP response without proxying anywhere.
* Added support for proxying 100-Continue responses.
* Added DOWNSTREAM_LOCAL_ADDRESS, DOWNSTREAM_LOCAL_ADDRESS_WITHOUT_PORT header formatters, and
  DOWNSTREAM_LOCAL_ADDRESS access log formatter.
* Added support for HTTPS redirects on specific routes.
* Added the ability to pass a URL encoded Pem encoded peer certificate in the x-forwarded-client-cert header.
* Added support for abstract unix domain sockets on linux. The abstract
  namespace can be used by prepending '@' to a socket path.
* Added `GEORADIUS_RO` and `GEORADIUSBYMEMBER_RO` to the Redis command splitter whitelist.
* Added support for trusting additional hops in the X-Forwarded-For request header.
