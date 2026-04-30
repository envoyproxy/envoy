#pragma once

#include <string>

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Tracing {

/**
 * Standardized tracing tag identifiers.
 */
enum class TagName {
  Component,
  DbInstance,
  DbStatement,
  DbUser,
  DbType,
  Error,
  HttpMethod,
  HttpStatusCode,
  HttpUrl,
  MessageBusDestination,
  PeerAddress,
  PeerHostname,
  PeerIpv4,
  PeerIpv6,
  PeerPort,
  PeerService,
  SpanKind,
  DownstreamCluster,
  ErrorReason,
  GrpcAuthority,
  GrpcContentType,
  GrpcMessage,
  GrpcPath,
  GrpcStatusCode,
  GrpcTimeout,
  GuidXClientTraceId,
  GuidXRequestId,
  HttpProtocol,
  NodeId,
  RequestSize,
  ResponseFlags,
  ResponseSize,
  RetryCount,
  Status,
  UpstreamAddress,
  UpstreamCluster,
  UpstreamClusterName,
  UserAgent,
  Zone
};

/**
 * Encapsulates a tracing tag default name and its semantic enum identifier.
 */
class Tag {
public:
  Tag(absl::string_view name, TagName id, absl::string_view sem_conv_name = "")
      : name_(name), id_(id), sem_conv_name_(sem_conv_name.empty() ? name : sem_conv_name) {}

  absl::string_view name() const { return name_; }
  TagName id() const { return id_; }
  absl::string_view semConvName() const { return sem_conv_name_; }

  /**
   * Implicit conversion to string_view enables transparent use at legacy call sites
   * that expect string names.
   */
  operator absl::string_view() const { return name_; }

private:
  const std::string name_;
  const TagName id_;
  const std::string sem_conv_name_;
};

} // namespace Tracing
} // namespace Envoy
