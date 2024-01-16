#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "envoy/access_log/access_log.h"
#include "envoy/access_log/access_log_config.h"
#include "envoy/common/random_generator.h"
#include "envoy/config/accesslog/v3/accesslog.pb.h"
#include "envoy/config/typed_config.h"
#include "envoy/runtime/runtime.h"
#include "envoy/type/v3/percent.pb.h"

#include "source/common/common/matchers.h"
#include "source/common/common/utility.h"
#include "source/common/config/utility.h"
#include "source/common/formatter/http_specific_formatter.h"
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
                             Server::Configuration::FactoryContext& context);
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
  bool evaluate(const Formatter::HttpFormatterContext& context,
                const StreamInfo::StreamInfo& info) const override;
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
  bool evaluate(const Formatter::HttpFormatterContext& context,
                const StreamInfo::StreamInfo& info) const override;
};

/**
 * Base operator filter, compose other filters with operation
 */
class OperatorFilter : public Filter {
public:
  OperatorFilter(
      const Protobuf::RepeatedPtrField<envoy::config::accesslog::v3::AccessLogFilter>& configs,
      Server::Configuration::FactoryContext& context);

protected:
  std::vector<FilterPtr> filters_;
};

/**
 * *And* operator filter, apply logical *and* operation to all of the sub filters.
 */
class AndFilter : public OperatorFilter {
public:
  AndFilter(const envoy::config::accesslog::v3::AndFilter& config,
            Server::Configuration::FactoryContext& context);

  // AccessLog::Filter
  bool evaluate(const Formatter::HttpFormatterContext& context,
                const StreamInfo::StreamInfo& info) const override;
};

/**
 * *Or* operator filter, apply logical *or* operation to all of the sub filters.
 */
class OrFilter : public OperatorFilter {
public:
  OrFilter(const envoy::config::accesslog::v3::OrFilter& config,
           Server::Configuration::FactoryContext& context);

  // AccessLog::Filter
  bool evaluate(const Formatter::HttpFormatterContext& context,
                const StreamInfo::StreamInfo& info) const override;
};

/**
 * Filter out health check requests.
 */
class NotHealthCheckFilter : public Filter {
public:
  NotHealthCheckFilter() = default;

  // AccessLog::Filter
  bool evaluate(const Formatter::HttpFormatterContext& context,
                const StreamInfo::StreamInfo& info) const override;
};

/**
 * Filter traceable requests.
 */
class TraceableRequestFilter : public Filter {
public:
  // AccessLog::Filter
  bool evaluate(const Formatter::HttpFormatterContext& context,
                const StreamInfo::StreamInfo& info) const override;
};

/**
 * Filter that uses a runtime feature key to check if the log should be written.
 */
class RuntimeFilter : public Filter {
public:
  RuntimeFilter(const envoy::config::accesslog::v3::RuntimeFilter& config, Runtime::Loader& runtime,
                Random::RandomGenerator& random);

  // AccessLog::Filter
  bool evaluate(const Formatter::HttpFormatterContext& context,
                const StreamInfo::StreamInfo& info) const override;

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
  bool evaluate(const Formatter::HttpFormatterContext& context,
                const StreamInfo::StreamInfo& info) const override;

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
  bool evaluate(const Formatter::HttpFormatterContext& context,
                const StreamInfo::StreamInfo& info) const override;

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
  bool evaluate(const Formatter::HttpFormatterContext& context,
                const StreamInfo::StreamInfo& info) const override;

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
 * Filters requests based on access log type
 */
class LogTypeFilter : public Filter {
public:
  using LogTypeHashSet = absl::flat_hash_set<AccessLogType>;

  LogTypeFilter(const envoy::config::accesslog::v3::LogTypeFilter& filter_config);

  bool evaluate(const Formatter::HttpFormatterContext& context,
                const StreamInfo::StreamInfo& info) const override;

private:
  LogTypeHashSet types_;
  bool exclude_;
};

/**
 * Filters requests based on dynamic metadata
 */
class MetadataFilter : public Filter {
public:
  MetadataFilter(const envoy::config::accesslog::v3::MetadataFilter& filter_config);

  bool evaluate(const Formatter::HttpFormatterContext& context,
                const StreamInfo::StreamInfo& info) const override;

private:
  Matchers::ValueMatcherConstSharedPtr present_matcher_;
  Matchers::ValueMatcherConstSharedPtr value_matcher_;

  std::vector<std::string> path_;

  const bool default_match_;
  const std::string filter_;
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
  static InstanceSharedPtr fromProto(const envoy::config::accesslog::v3::AccessLog& config,
                                     Server::Configuration::FactoryContext& context);

  /**
   * Template method to create an access log filter from proto configuration for non-HTTP access
   * loggers.
   */
  template <class Context>
  static FilterBasePtr<Context>
  accessLogFilterFromProto(const envoy::config::accesslog::v3::AccessLogFilter& config,
                           Server::Configuration::FactoryContext& context) {
    if (!config.has_extension_filter()) {
      ExceptionUtil::throwEnvoyException(
          "Access log filter: only extension filter is supported by non-HTTP access loggers.");
    }

    auto& factory = Config::Utility::getAndCheckFactory<ExtensionFilterFactoryBase<Context>>(
        config.extension_filter());
    return factory.createFilter(config.extension_filter(), context);
  }

  /**
   * Template method to create an access logger instance from proto configuration for non-HTTP
   * access loggers.
   */
  template <class Context>
  static InstanceBaseSharedPtr<Context>
  accessLoggerFromProto(const envoy::config::accesslog::v3::AccessLog& config,
                        Server::Configuration::FactoryContext& context) {
    FilterBasePtr<Context> filter;
    if (config.has_filter()) {
      filter = accessLogFilterFromProto<Context>(config.filter(), context);
    }

    auto& factory =
        Config::Utility::getAndCheckFactory<AccessLogInstanceFactoryBase<Context>>(config);
    ProtobufTypes::MessagePtr message = Config::Utility::translateToFactoryConfig(
        config, context.messageValidationVisitor(), factory);

    return factory.createAccessLogInstance(*message, std::move(filter), context);
  }
};

} // namespace AccessLog
} // namespace Envoy
