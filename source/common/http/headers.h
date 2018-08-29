#pragma once

#include <string>

#include "envoy/http/header_map.h"

#include "common/singleton/const_singleton.h"

namespace Envoy {
namespace Http {

/**
 * Constant HTTP headers and values. All lower case.
 */
class HeaderValues {
public:
  const LowerCaseString Accept{"accept"};
  const LowerCaseString AcceptEncoding{"accept-encoding"};
  const LowerCaseString AccessControlRequestHeaders{"access-control-request-headers"};
  const LowerCaseString AccessControlRequestMethod{"access-control-request-method"};
  const LowerCaseString AccessControlAllowOrigin{"access-control-allow-origin"};
  const LowerCaseString AccessControlAllowHeaders{"access-control-allow-headers"};
  const LowerCaseString AccessControlAllowMethods{"access-control-allow-methods"};
  const LowerCaseString AccessControlExposeHeaders{"access-control-expose-headers"};
  const LowerCaseString AccessControlMaxAge{"access-control-max-age"};
  const LowerCaseString AccessControlAllowCredentials{"access-control-allow-credentials"};
  const LowerCaseString Authorization{"authorization"};
  const LowerCaseString CacheControl{"cache-control"};
  const LowerCaseString ClientTraceId{"x-client-trace-id"};
  const LowerCaseString Connection{"connection"};
  const LowerCaseString ContentEncoding{"content-encoding"};
  const LowerCaseString ContentLength{"content-length"};
  const LowerCaseString ContentType{"content-type"};
  const LowerCaseString Cookie{"cookie"};
  const LowerCaseString Date{"date"};
  const LowerCaseString EnvoyDownstreamServiceCluster{"x-envoy-downstream-service-cluster"};
  const LowerCaseString EnvoyDownstreamServiceNode{"x-envoy-downstream-service-node"};
  const LowerCaseString EnvoyExternalAddress{"x-envoy-external-address"};
  const LowerCaseString EnvoyForceTrace{"x-envoy-force-trace"};
  const LowerCaseString EnvoyImmediateHealthCheckFail{"x-envoy-immediate-health-check-fail"};
  const LowerCaseString EnvoyInternalRequest{"x-envoy-internal"};
  const LowerCaseString EnvoyIpTags{"x-envoy-ip-tags"};
  const LowerCaseString EnvoyMaxRetries{"x-envoy-max-retries"};
  const LowerCaseString EnvoyOriginalDstHost{"x-envoy-original-dst-host"};
  const LowerCaseString EnvoyOriginalPath{"x-envoy-original-path"};
  const LowerCaseString EnvoyOverloaded{"x-envoy-overloaded"};
  const LowerCaseString EnvoyRetryOn{"x-envoy-retry-on"};
  const LowerCaseString EnvoyRetryGrpcOn{"x-envoy-retry-grpc-on"};
  const LowerCaseString EnvoyUpstreamAltStatName{"x-envoy-upstream-alt-stat-name"};
  const LowerCaseString EnvoyUpstreamCanary{"x-envoy-upstream-canary"};
  const LowerCaseString EnvoyUpstreamRequestTimeoutAltResponse{
      "x-envoy-upstream-rq-timeout-alt-response"};
  const LowerCaseString EnvoyUpstreamRequestTimeoutMs{"x-envoy-upstream-rq-timeout-ms"};
  const LowerCaseString EnvoyUpstreamRequestPerTryTimeoutMs{
      "x-envoy-upstream-rq-per-try-timeout-ms"};
  const LowerCaseString EnvoyExpectedRequestTimeoutMs{"x-envoy-expected-rq-timeout-ms"};
  const LowerCaseString EnvoyUpstreamServiceTime{"x-envoy-upstream-service-time"};
  const LowerCaseString EnvoyUpstreamHealthCheckedCluster{"x-envoy-upstream-healthchecked-cluster"};
  const LowerCaseString EnvoyDecoratorOperation{"x-envoy-decorator-operation"};
  const LowerCaseString Etag{"etag"};
  const LowerCaseString Expect{"expect"};
  const LowerCaseString ForwardedClientCert{"x-forwarded-client-cert"};
  const LowerCaseString ForwardedFor{"x-forwarded-for"};
  const LowerCaseString ForwardedProto{"x-forwarded-proto"};
  const LowerCaseString GrpcMessage{"grpc-message"};
  const LowerCaseString GrpcStatus{"grpc-status"};
  const LowerCaseString GrpcTimeout{"grpc-timeout"};
  const LowerCaseString GrpcAcceptEncoding{"grpc-accept-encoding"};
  const LowerCaseString Host{":authority"};
  const LowerCaseString HostLegacy{"host"};
  const LowerCaseString KeepAlive{"keep-alive"};
  const LowerCaseString LastModified{"last-modified"};
  const LowerCaseString Location{"location"};
  const LowerCaseString Method{":method"};
  const LowerCaseString NoChunks{":no-chunks"};
  const LowerCaseString Origin{"origin"};
  const LowerCaseString OtSpanContext{"x-ot-span-context"};
  const LowerCaseString Path{":path"};
  const LowerCaseString Protocol{":protocol"};
  const LowerCaseString ProxyConnection{"proxy-connection"};
  const LowerCaseString Referer{"referer"};
  const LowerCaseString RequestId{"x-request-id"};
  const LowerCaseString Scheme{":scheme"};
  const LowerCaseString Server{"server"};
  const LowerCaseString SetCookie{"set-cookie"};
  const LowerCaseString Status{":status"};
  const LowerCaseString TransferEncoding{"transfer-encoding"};
  const LowerCaseString TE{"te"};
  const LowerCaseString Upgrade{"upgrade"};
  const LowerCaseString UserAgent{"user-agent"};
  const LowerCaseString Vary{"vary"};
  const LowerCaseString Via{"via"};
  const LowerCaseString XB3TraceId{"x-b3-traceid"};
  const LowerCaseString XB3SpanId{"x-b3-spanid"};
  const LowerCaseString XB3ParentSpanId{"x-b3-parentspanid"};
  const LowerCaseString XB3Sampled{"x-b3-sampled"};
  const LowerCaseString XB3Flags{"x-b3-flags"};
  const LowerCaseString XContentTypeOptions{"x-content-type-options"};
  const LowerCaseString XSquashDebug{"x-squash-debug"};

  struct {
    const std::string Close{"close"};
    const std::string KeepAlive{"keep-alive"};
    const std::string Upgrade{"upgrade"};
  } ConnectionValues;

  struct {
    const std::string WebSocket{"websocket"};
  } UpgradeValues;

  struct {
    const std::string NoCache{"no-cache"};
    const std::string NoCacheMaxAge0{"no-cache, max-age=0"};
    const std::string NoTransform{"no-transform"};
  } CacheControlValues;

  struct {
    const std::string Text{"text/plain"};
    const std::string TextEventStream{"text/event-stream"};
    const std::string TextUtf8{"text/plain; charset=UTF-8"}; // TODO(jmarantz): fold this into Text
    const std::string Html{"text/html; charset=UTF-8"};
    const std::string Grpc{"application/grpc"};
    const std::string GrpcWeb{"application/grpc-web"};
    const std::string GrpcWebProto{"application/grpc-web+proto"};
    const std::string GrpcWebText{"application/grpc-web-text"};
    const std::string GrpcWebTextProto{"application/grpc-web-text+proto"};
    const std::string Json{"application/json"};
  } ContentTypeValues;

  struct {
    const std::string True{"true"};
  } EnvoyImmediateHealthCheckFailValues;

  struct {
    const std::string True{"true"};
  } EnvoyInternalRequestValues;

  struct {
    const std::string True{"true"};
  } EnvoyOverloadedValues;

  struct {
    const std::string _5xx{"5xx"};
    const std::string GatewayError{"gateway-error"};
    const std::string ConnectFailure{"connect-failure"};
    const std::string RefusedStream{"refused-stream"};
    const std::string Retriable4xx{"retriable-4xx"};
  } EnvoyRetryOnValues;

  struct {
    const std::string Cancelled{"cancelled"};
    const std::string DeadlineExceeded{"deadline-exceeded"};
    const std::string ResourceExhausted{"resource-exhausted"};
    const std::string Unavailable{"unavailable"};
  } EnvoyRetryOnGrpcValues;

  struct {
    const std::string _100Continue{"100-continue"};
  } ExpectValues;

  struct {
    const std::string Connect{"CONNECT"};
    const std::string Get{"GET"};
    const std::string Head{"HEAD"};
    const std::string Post{"POST"};
    const std::string Options{"OPTIONS"};
  } MethodValues;

  struct {
    const std::string Http{"http"};
    const std::string Https{"https"};
  } SchemeValues;

  struct {
    const std::string Chunked{"chunked"};
    const std::string Deflate{"deflate"};
    const std::string Gzip{"gzip"};
  } TransferEncodingValues;

  struct {
    const std::string EnvoyHealthChecker{"Envoy/HC"};
  } UserAgentValues;

  struct {
    const std::string Default{"identity,deflate,gzip"};
  } GrpcAcceptEncodingValues;

  struct {
    const std::string Trailers{"trailers"};
  } TEValues;

  struct {
    const std::string Nosniff{"nosniff"};
  } XContentTypeOptionValues;

  struct {
    const std::string True{"true"};
  } CORSValues;

  struct {
    const std::string Http10String{"HTTP/1.0"};
    const std::string Http11String{"HTTP/1.1"};
    const std::string Http2String{"HTTP/2"};
  } ProtocolStrings;

  struct {
    const std::string Gzip{"gzip"};
    const std::string Identity{"identity"};
    const std::string Wildcard{"*"};
  } AcceptEncodingValues;

  struct {
    const std::string Gzip{"gzip"};
  } ContentEncodingValues;

  struct {
    const std::string AcceptEncoding{"Accept-Encoding"};
    const std::string Wildcard{"*"};
  } VaryValues;

  struct {
    const std::string All{"*"};
  } AccessControlAllowOriginValue;
};

typedef ConstSingleton<HeaderValues> Headers;

} // namespace Http
} // namespace Envoy
