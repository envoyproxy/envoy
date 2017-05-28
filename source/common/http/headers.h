#pragma once

#include <string>

#include "envoy/http/header_map.h"

#include "common/common/singleton.h"

namespace Envoy {
namespace Http {

/**
 * Constant HTTP headers and values. All lower case.
 */
class HeaderValues {
public:
  const LowerCaseString Accept{"accept"};
  const LowerCaseString Authorization{"authorization"};
  const LowerCaseString ClientTraceId{"x-client-trace-id"};
  const LowerCaseString Connection{"connection"};
  const LowerCaseString ContentLength{"content-length"};
  const LowerCaseString ContentType{"content-type"};
  const LowerCaseString Cookie{"cookie"};
  const LowerCaseString Date{"date"};
  const LowerCaseString EnvoyDownstreamServiceCluster{"x-envoy-downstream-service-cluster"};
  const LowerCaseString EnvoyDownstreamServiceNode{"x-envoy-downstream-service-node"};
  const LowerCaseString EnvoyExternalAddress{"x-envoy-external-address"};
  const LowerCaseString EnvoyForceTrace{"x-envoy-force-trace"};
  const LowerCaseString EnvoyInternalRequest{"x-envoy-internal"};
  const LowerCaseString EnvoyMaxRetries{"x-envoy-max-retries"};
  const LowerCaseString EnvoyOriginalPath{"x-envoy-original-path"};
  const LowerCaseString EnvoyRetryOn{"x-envoy-retry-on"};
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
  const LowerCaseString Expect{"expect"};
  const LowerCaseString ForwardedFor{"x-forwarded-for"};
  const LowerCaseString ForwardedProto{"x-forwarded-proto"};
  const LowerCaseString GrpcMessage{"grpc-message"};
  const LowerCaseString GrpcStatus{"grpc-status"};
  const LowerCaseString GrpcAcceptEncoding{"grpc-accept-encoding"};
  const LowerCaseString Host{":authority"};
  const LowerCaseString HostLegacy{"host"};
  const LowerCaseString KeepAlive{"keep-alive"};
  const LowerCaseString Location{"location"};
  const LowerCaseString Method{":method"};
  const LowerCaseString OtSpanContext{"x-ot-span-context"};
  const LowerCaseString Path{":path"};
  const LowerCaseString ProxyConnection{"proxy-connection"};
  const LowerCaseString RequestId{"x-request-id"};
  const LowerCaseString Scheme{":scheme"};
  const LowerCaseString Server{"server"};
  const LowerCaseString Status{":status"};
  const LowerCaseString TransferEncoding{"transfer-encoding"};
  const LowerCaseString TE{"te"};
  const LowerCaseString Upgrade{"upgrade"};
  const LowerCaseString UserAgent{"user-agent"};
  const LowerCaseString XB3TraceId{"x-b3-traceid"};
  const LowerCaseString XB3SpanId{"x-b3-spanid"};
  const LowerCaseString XB3ParentSpanId{"x-b3-parentspanid"};
  const LowerCaseString XB3Sampled{"x-b3-sampled"};
  const LowerCaseString XB3Flags{"x-b3-flags"};

  struct {
    const std::string Close{"close"};
  } ConnectionValues;

  struct {
    const std::string Text{"text/plain"};
    const std::string Grpc{"application/grpc"};
    const std::string GrpcWeb{"application/grpc-web"};
    const std::string GrpcWebText{"application/grpc-web-text"};
  } ContentTypeValues;

  struct {
    const std::string True{"true"};
  } EnvoyInternalRequestValues;

  struct {
    const std::string _5xx{"5xx"};
    const std::string ConnectFailure{"connect-failure"};
    const std::string RefusedStream{"refused-stream"};
    const std::string Retriable4xx{"retriable-4xx"};
  } EnvoyRetryOnValues;

  struct {
    const std::string _100Continue{"100-continue"};
  } ExpectValues;

  struct {
    const std::string Get{"GET"};
    const std::string Head{"HEAD"};
    const std::string Post{"POST"};
  } MethodValues;

  struct {
    const std::string Http{"http"};
    const std::string Https{"https"};
  } SchemeValues;

  struct {
    const std::string Chunked{"chunked"};
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
};

typedef ConstSingleton<HeaderValues> Headers;

} // Http
} // Envoy
