#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "envoy/access_log/access_log.h"
#include "envoy/common/random_generator.h"
#include "envoy/config/accesslog/v3/accesslog.pb.h"
#include "envoy/config/typed_config.h"
#include "envoy/runtime/runtime.h"
#include "envoy/server/access_log_config.h"
#include "envoy/type/v3/percent.pb.h"

#include "source/common/common/matchers.h"
#include "source/common/grpc/status.h"
#include "source/common/http/header_utility.h"
#include "source/common/protobuf/protobuf.h"

#include "absl/container/node_hash_set.h"
#include "absl/hash/hash.h"

namespace Envoy {
namespace AccessLog {

/**
 * Access log filter factory that reads from proto.
 */
class FilterFactory {
public:
  /**
   * Read a filter definition from proto and instantiate a concrete filter class.
   */
  static FilterPtr fromProto(const envoy::config::accesslog::v3::AccessLogFilter& config,
                             Runtime::Loader& runtime, Random::RandomGenerator& random,
                             ProtobufMessage::ValidationVisitor& validation_visitor);
};

/**
 * Base implementation of an access log filter that performs comparisons.
 */
class ComparisonFilter : public Filter {
protected:
  ComparisonFilter(const envoy::config::accesslog::v3::ComparisonFilter& config,
                   Runtime::Loader& runtime);

  bool compareAgainstValue(uint64_t lhs) const;

  envoy::config::accesslog::v3::ComparisonFilter config_;
  Runtime::Loader& runtime_;
};

/**
 * Filter on response status code.
 */
class StatusCodeFilter : public ComparisonFilter {
public:
  StatusCodeFilter(const envoy::config::accesslog::v3::StatusCodeFilter& config,
                   Runtime::Loader& runtime)
      : ComparisonFilter(config.comparison(), runtime) {}

  // AccessLog::Filter
  bool evaluate(const StreamInfo::StreamInfo& info, const Http::RequestHeaderMap& request_headers,
                const Http::ResponseHeaderMap& response_headers,
                const Http::ResponseTrailerMap& response_trailers) const override;
};

/**
 * Filter on total request/response duration.
 */
class DurationFilter : public ComparisonFilter {
public:
  DurationFilter(const envoy::config::accesslog::v3::DurationFilter& config,
                 Runtime::Loader& runtime)
      : ComparisonFilter(config.comparison(), runtime) {}

  // AccessLog::Filter
  bool evaluate(const StreamInfo::StreamInfo& info, const Http::RequestHeaderMap& request_headers,
                const Http::ResponseHeaderMap& response_headers,
                const Http::ResponseTrailerMap& response_trailers) const override;
};

/**
 * Base operator filter, compose other filters with operation
 */
class OperatorFilter : public Filter {
public:
  OperatorFilter(
      const Protobuf::RepeatedPtrField<envoy::config::accesslog::v3::AccessLogFilter>& configs,
      Runtime::Loader& runtime, Random::RandomGenerator& random,
      ProtobufMessage::ValidationVisitor& validation_visitor);

protected:
  std::vector<FilterPtr> filters_;
};

/**
 * *And* operator filter, apply logical *and* operation to all of the sub filters.
 */
class AndFilter : public OperatorFilter {
public:
  AndFilter(const envoy::config::accesslog::v3::AndFilter& config, Runtime::Loader& runtime,
            Random::RandomGenerator& random,
            ProtobufMessage::ValidationVisitor& validation_visitor);

  // AccessLog::Filter
  bool evaluate(const StreamInfo::StreamInfo& info, const Http::RequestHeaderMap& request_headers,
                const Http::ResponseHeaderMap& response_headers,
                const Http::ResponseTrailerMap& response_trailers) const override;
};

/**
 * *Or* operator filter, apply logical *or* operation to all of the sub filters.
 */
class OrFilter : public OperatorFilter {
public:
  OrFilter(const envoy::config::accesslog::v3::OrFilter& config, Runtime::Loader& runtime,
           Random::RandomGenerator& random, ProtobufMessage::ValidationVisitor& validation_visitor);

  // AccessLog::Filter
  bool evaluate(const StreamInfo::StreamInfo& info, const Http::RequestHeaderMap& request_headers,
                const Http::ResponseHeaderMap& response_headers,
                const Http::ResponseTrailerMap& response_trailers) const override;
};

/**
 * Filter out health check requests.
 */
class NotHealthCheckFilter : public Filter {
public:
  NotHealthCheckFilter() = default;

  // AccessLog::Filter
  bool evaluate(const StreamInfo::StreamInfo& info, const Http::RequestHeaderMap& request_headers,
                const Http::ResponseHeaderMap& response_headers,
                const Http::ResponseTrailerMap& response_trailers) const override;
};

/**
 * Filter traceable requests.
 */
class TraceableRequestFilter : public Filter {
public:
  // AccessLog::Filter
  bool evaluate(const StreamInfo::StreamInfo& info, const Http::RequestHeaderMap& request_headers,
                const Http::ResponseHeaderMap& response_headers,
                const Http::ResponseTrailerMap& response_trailers) const override;
};

/**
 * Filter that uses a runtime feature key to check if the log should be written.
 */
class RuntimeFilter : public Filter {
public:
  RuntimeFilter(const envoy::config::accesslog::v3::RuntimeFilter& config, Runtime::Loader& runtime,
                Random::RandomGenerator& random);

  // AccessLog::Filter
  bool evaluate(const StreamInfo::StreamInfo& info, const Http::RequestHeaderMap& request_headers,
                const Http::ResponseHeaderMap& response_headers,
                const Http::ResponseTrailerMap& response_trailers) const override;

private:
  Runtime::Loader& runtime_;
  Random::RandomGenerator& random_;
  const std::string runtime_key_;
  const envoy::type::v3::FractionalPercent percent_;
  const bool use_independent_randomness_;
};

/**
 * Filter based on headers.
 */
class HeaderFilter : public Filter {
public:
  HeaderFilter(const envoy::config::accesslog::v3::HeaderFilter& config);

  // AccessLog::Filter
  bool evaluate(const StreamInfo::StreamInfo& info, const Http::RequestHeaderMap& request_headers,
                const Http::ResponseHeaderMap& response_headers,
                const Http::ResponseTrailerMap& response_trailers) const override;

private:
  const Http::HeaderUtility::HeaderDataPtr header_data_;
};

/**
 * Filter requests that had a response with an Envoy response flag set.
 */
class ResponseFlagFilter : public Filter {
public:
  ResponseFlagFilter(const envoy::config::accesslog::v3::ResponseFlagFilter& config);

  // AccessLog::Filter
  bool evaluate(const StreamInfo::StreamInfo& info, const Http::RequestHeaderMap& request_headers,
                const Http::ResponseHeaderMap& response_headers,
                const Http::ResponseTrailerMap& response_trailers) const override;

private:
  uint64_t configured_flags_{};
};

/**
 * Filters requests that have a response with a gRPC status. Because the gRPC protocol does not
 * guarantee a gRPC status code, if a gRPC status code is not available, then the filter will infer
 * the gRPC status code from an HTTP status code if available.
 */
class GrpcStatusFilter : public Filter {
public:
  using GrpcStatusHashSet =
      absl::node_hash_set<Grpc::Status::GrpcStatus, absl::Hash<Grpc::Status::GrpcStatus>>;

  GrpcStatusFilter(const envoy::config::accesslog::v3::GrpcStatusFilter& config);

  // AccessLog::Filter
  bool evaluate(const StreamInfo::StreamInfo& info, const Http::RequestHeaderMap& request_headers,
                const Http::ResponseHeaderMap& response_headers,
                const Http::ResponseTrailerMap& response_trailers) const override;

private:
  GrpcStatusHashSet statuses_;
  bool exclude_;

  /**
   * Converts a Protobuf representation of a gRPC status into the equivalent code version of a gRPC
   * status.
   */
  Grpc::Status::GrpcStatus
  protoToGrpcStatus(envoy::config::accesslog::v3::GrpcStatusFilter::Status status) const;
};

/**
 * Filters requests based on dynamic metadata
 */
class MetadataFilter : public Filter {
public:
  MetadataFilter(const envoy::config::accesslog::v3::MetadataFilter& filter_config);

  bool evaluate(const StreamInfo::StreamInfo& info, const Http::RequestHeaderMap& request_headers,
                const Http::ResponseHeaderMap& response_headers,
                const Http::ResponseTrailerMap& response_trailers) const override;

private:
  Matchers::ValueMatcherConstSharedPtr present_matcher_;
  Matchers::ValueMatcherConstSharedPtr value_matcher_;

  std::vector<std::string> path_;

  const bool default_match_;
  const std::string filter_;
};

/**
 * Extension filter factory that reads from ExtensionFilter proto.
 */
class ExtensionFilterFactory : public Config::TypedFactory {
public:
  ~ExtensionFilterFactory() override = default;

  /**
   * Create a particular extension filter implementation from a config proto. If the
   * implementation is unable to produce a filter with the provided parameters, it should throw an
   * EnvoyException. The returned pointer should never be nullptr.
   * @param config supplies the custom configuration for this filter type.
   * @param runtime supplies the runtime loader.
   * @param random supplies the random generator.
   * @return an instance of extension filter implementation from a config proto.
   */
  virtual FilterPtr createFilter(const envoy::config::accesslog::v3::ExtensionFilter& config,
                                 Runtime::Loader& runtime, Random::RandomGenerator& random) PURE;

  std::string category() const override { return "envoy.access_loggers.extension_filters"; }
};

/**
 * Access log factory that reads the configuration from proto.
 */
class AccessLogFactory {
public:
  /**
   * Read a filter definition from proto and instantiate an Instance. This method is used
   * to create access log instances that need access to listener properties.
   */
  static InstanceSharedPtr
  fromProto(const envoy::config::accesslog::v3::AccessLog& config,
            Server::Configuration::ListenerAccessLogFactoryContext& context);

  /**
   * Read a filter definition from proto and instantiate an Instance. This method does not
   * have access to listener properties, for example for access loggers of admin interface.
   */
  static InstanceSharedPtr fromProto(const envoy::config::accesslog::v3::AccessLog& config,
                                     Server::Configuration::CommonFactoryContext& context);
};

} // namespace AccessLog
} // namespace Envoy
