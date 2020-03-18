#pragma once

#include <string>

#include "envoy/http/header_map.h"

#include "common/singleton/const_singleton.h"
#include "common/singleton/threadsafe_singleton.h"

namespace Envoy {
namespace Http {

// This class allows early override of the x-envoy prefix from bootstrap config,
// so that servers can configure their own x-custom-string prefix.
//
// Once the HeaderValues const singleton has been created, changing the prefix
// is disallowed. Essentially this is write-once then read-only.
class PrefixValue {
public:
  const char* prefix() {
    absl::WriterMutexLock lock(&m_);
    read_ = true;
    return prefix_.c_str();
  }

  // The char* prefix is used directly, so must be available for the interval where prefix() may be
  // called.
  void setPrefix(const char* prefix) {
    absl::WriterMutexLock lock(&m_);
    // The check for unchanged string is purely for integration tests - this
    // should not happen in production.
    RELEASE_ASSERT(!read_ || prefix_ == std::string(prefix),
                   "Attempting to change the header prefix after it has been used!");
    if (!read_) {
      prefix_ = prefix;
    }
  }

private:
  absl::Mutex m_;
  bool read_ = false;
  std::string prefix_ = "x-envoy";
};

/**
 * Constant HTTP headers and values. All lower case.
 */
class HeaderValues {
public:
  const char* prefix() { return ThreadSafeSingleton<PrefixValue>::get().prefix(); }

  const LowerCaseString Accept{"accept"};
  const LowerCaseString AcceptEncoding{"accept-encoding"};
  const LowerCaseString AccessControlRequestMethod{"access-control-request-method"};
  const LowerCaseString AccessControlAllowOrigin{"access-control-allow-origin"};
  const LowerCaseString AccessControlAllowHeaders{"access-control-allow-headers"};
  const LowerCaseString AccessControlAllowMethods{"access-control-allow-methods"};
  const LowerCaseString AccessControlExposeHeaders{"access-control-expose-headers"};
  const LowerCaseString AccessControlMaxAge{"access-control-max-age"};
  const LowerCaseString AccessControlAllowCredentials{"access-control-allow-credentials"};
  const LowerCaseString Age{"age"};
  const LowerCaseString Authorization{"authorization"};
  const LowerCaseString ProxyAuthenticate{"proxy-authenticate"};
  const LowerCaseString ProxyAuthorization{"proxy-authorization"};
  const LowerCaseString CacheControl{"cache-control"};
  const LowerCaseString ClientTraceId{"x-client-trace-id"};
  const LowerCaseString Connection{"connection"};
  const LowerCaseString ContentEncoding{"content-encoding"};
  const LowerCaseString ContentLength{"content-length"};
  const LowerCaseString ContentType{"content-type"};
  const LowerCaseString Cookie{"cookie"};
  const LowerCaseString Date{"date"};
  const LowerCaseString EnvoyAttemptCount{absl::StrCat(prefix(), "-attempt-count")};
  const LowerCaseString EnvoyAuthPartialBody{absl::StrCat(prefix(), "-auth-partial-body")};
  const LowerCaseString EnvoyCluster{absl::StrCat(prefix(), "-cluster")};
  const LowerCaseString EnvoyDegraded{absl::StrCat(prefix(), "-degraded")};
  const LowerCaseString EnvoyDownstreamServiceCluster{
      absl::StrCat(prefix(), "-downstream-service-cluster")};
  const LowerCaseString EnvoyDownstreamServiceNode{
      absl::StrCat(prefix(), "-downstream-service-node")};
  const LowerCaseString EnvoyExternalAddress{absl::StrCat(prefix(), "-external-address")};
  const LowerCaseString EnvoyForceTrace{absl::StrCat(prefix(), "-force-trace")};
  const LowerCaseString EnvoyHedgeOnPerTryTimeout{
      absl::StrCat(prefix(), "-hedge-on-per-try-timeout")};
  const LowerCaseString EnvoyImmediateHealthCheckFail{
      absl::StrCat(prefix(), "-immediate-health-check-fail")};
  const LowerCaseString EnvoyOriginalUrl{absl::StrCat(prefix(), "-original-url")};
  const LowerCaseString EnvoyInternalRequest{absl::StrCat(prefix(), "-internal")};
  const LowerCaseString EnvoyIpTags{absl::StrCat(prefix(), "-ip-tags")};
  const LowerCaseString EnvoyMaxRetries{absl::StrCat(prefix(), "-max-retries")};
  const LowerCaseString EnvoyNotForwarded{absl::StrCat(prefix(), "-not-forwarded")};
  const LowerCaseString EnvoyOriginalDstHost{absl::StrCat(prefix(), "-original-dst-host")};
  const LowerCaseString EnvoyOriginalPath{absl::StrCat(prefix(), "-original-path")};
  const LowerCaseString EnvoyOverloaded{absl::StrCat(prefix(), "-overloaded")};
  const LowerCaseString EnvoyRateLimited{absl::StrCat(prefix(), "-ratelimited")};
  const LowerCaseString EnvoyRetryOn{absl::StrCat(prefix(), "-retry-on")};
  const LowerCaseString EnvoyRetryGrpcOn{absl::StrCat(prefix(), "-retry-grpc-on")};
  const LowerCaseString EnvoyRetriableStatusCodes{
      absl::StrCat(prefix(), "-retriable-status-codes")};
  const LowerCaseString EnvoyRetriableHeaderNames{
      absl::StrCat(prefix(), "-retriable-header-names")};
  const LowerCaseString EnvoyUpstreamAltStatName{absl::StrCat(prefix(), "-upstream-alt-stat-name")};
  const LowerCaseString EnvoyUpstreamCanary{absl::StrCat(prefix(), "-upstream-canary")};
  const LowerCaseString EnvoyUpstreamHostAddress{absl::StrCat(prefix(), "-upstream-host-address")};
  const LowerCaseString EnvoyUpstreamHostname{absl::StrCat(prefix(), "-upstream-hostname")};
  const LowerCaseString EnvoyUpstreamRequestTimeoutAltResponse{
      absl::StrCat(prefix(), "-upstream-rq-timeout-alt-response")};
  const LowerCaseString EnvoyUpstreamRequestTimeoutMs{
      absl::StrCat(prefix(), "-upstream-rq-timeout-ms")};
  const LowerCaseString EnvoyUpstreamRequestPerTryTimeoutMs{
      absl::StrCat(prefix(), "-upstream-rq-per-try-timeout-ms")};
  const LowerCaseString EnvoyExpectedRequestTimeoutMs{
      absl::StrCat(prefix(), "-expected-rq-timeout-ms")};
  const LowerCaseString EnvoyUpstreamServiceTime{absl::StrCat(prefix(), "-upstream-service-time")};
  const LowerCaseString EnvoyUpstreamHealthCheckedCluster{
      absl::StrCat(prefix(), "-upstream-healthchecked-cluster")};
  const LowerCaseString EnvoyDecoratorOperation{absl::StrCat(prefix(), "-decorator-operation")};
  const LowerCaseString Etag{"etag"};
  const LowerCaseString Expect{"expect"};
  const LowerCaseString Expires{"expires"};
  const LowerCaseString ForwardedClientCert{"x-forwarded-client-cert"};
  const LowerCaseString ForwardedFor{"x-forwarded-for"};
  const LowerCaseString ForwardedHost{"x-forwarded-host"};
  const LowerCaseString ForwardedProto{"x-forwarded-proto"};
  const LowerCaseString GrpcMessage{"grpc-message"};
  const LowerCaseString GrpcStatus{"grpc-status"};
  const LowerCaseString GrpcTimeout{"grpc-timeout"};
  const LowerCaseString GrpcAcceptEncoding{"grpc-accept-encoding"};
  const LowerCaseString GrpcStatusDetailsBin{"grpc-status-details-bin"};
  const LowerCaseString Host{":authority"};
  const LowerCaseString HostLegacy{"host"};
  const LowerCaseString Http2Settings{"http2-settings"};
  const LowerCaseString KeepAlive{"keep-alive"};
  const LowerCaseString Location{"location"};
  const LowerCaseString Method{":method"};
  const LowerCaseString NoChunks{":no-chunks"}; // Illegal pseudo-header used internally.
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
  const LowerCaseString WWWAuthenticate{"www-authenticate"};
  const LowerCaseString XContentTypeOptions{"x-content-type-options"};
  const LowerCaseString XSquashDebug{"x-squash-debug"};

  struct {
    const std::string Close{"close"};
    const std::string Http2Settings{"http2-settings"};
    const std::string KeepAlive{"keep-alive"};
    const std::string Upgrade{"upgrade"};
  } ConnectionValues;

  struct {
    const std::string H2c{"h2c"};
    const std::string WebSocket{"websocket"};
  } UpgradeValues;

  struct {
    const std::string NoCache{"no-cache"};
    const std::string NoCacheMaxAge0{"no-cache, max-age=0"};
    const std::string NoTransform{"no-transform"};
    const std::string Private{"private"};
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
    const std::string Protobuf{"application/x-protobuf"};
    const std::string FormUrlEncoded{"application/x-www-form-urlencoded"};
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
    const std::string True{"true"};
  } EnvoyRateLimitedValues;

  struct {
    const std::string _5xx{"5xx"};
    const std::string GatewayError{"gateway-error"};
    const std::string ConnectFailure{"connect-failure"};
    const std::string RefusedStream{"refused-stream"};
    const std::string Retriable4xx{"retriable-4xx"};
    const std::string RetriableStatusCodes{"retriable-status-codes"};
    const std::string RetriableHeaders{"retriable-headers"};
    const std::string Reset{"reset"};
  } EnvoyRetryOnValues;

  struct {
    const std::string Cancelled{"cancelled"};
    const std::string DeadlineExceeded{"deadline-exceeded"};
    const std::string ResourceExhausted{"resource-exhausted"};
    const std::string Unavailable{"unavailable"};
    const std::string Internal{"internal"};
  } EnvoyRetryOnGrpcValues;

  struct {
    const std::string _100Continue{"100-continue"};
  } ExpectValues;

  struct {
    const std::string Connect{"CONNECT"};
    const std::string Delete{"DELETE"};
    const std::string Get{"GET"};
    const std::string Head{"HEAD"};
    const std::string Options{"OPTIONS"};
    const std::string Patch{"PATCH"};
    const std::string Post{"POST"};
    const std::string Put{"PUT"};
    const std::string Trace{"TRACE"};
  } MethodValues;

  struct {
    const std::string Http{"http"};
    const std::string Https{"https"};
  } SchemeValues;

  struct {
    const std::string Chunked{"chunked"};
    const std::string Deflate{"deflate"};
    const std::string Gzip{"gzip"};
    const std::string Identity{"identity"};
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
    const std::string Http3String{"HTTP/3"};
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

using Headers = ConstSingleton<HeaderValues>;

} // namespace Http
} // namespace Envoy
