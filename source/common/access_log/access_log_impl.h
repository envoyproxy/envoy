#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "envoy/access_log/access_log.h"
#include "envoy/config/filter/accesslog/v2/accesslog.pb.h"
#include "envoy/runtime/runtime.h"
#include "envoy/server/access_log_config.h"

#include "common/http/header_utility.h"
#include "common/protobuf/protobuf.h"

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
  static FilterPtr fromProto(const envoy::config::filter::accesslog::v2::AccessLogFilter& config,
                             Runtime::Loader& runtime, Runtime::RandomGenerator& random);
};

/**
 * Base implementation of an access log filter that performs comparisons.
 */
class ComparisonFilter : public Filter {
protected:
  ComparisonFilter(const envoy::config::filter::accesslog::v2::ComparisonFilter& config,
                   Runtime::Loader& runtime);

  bool compareAgainstValue(uint64_t lhs);

  envoy::config::filter::accesslog::v2::ComparisonFilter config_;
  Runtime::Loader& runtime_;
};

/**
 * Filter on response status code.
 */
class StatusCodeFilter : public ComparisonFilter {
public:
  StatusCodeFilter(const envoy::config::filter::accesslog::v2::StatusCodeFilter& config,
                   Runtime::Loader& runtime)
      : ComparisonFilter(config.comparison(), runtime) {}

  // AccessLog::Filter
  bool evaluate(const RequestInfo::RequestInfo& info,
                const Http::HeaderMap& request_headers) override;
};

/**
 * Filter on total request/response duration.
 */
class DurationFilter : public ComparisonFilter {
public:
  DurationFilter(const envoy::config::filter::accesslog::v2::DurationFilter& config,
                 Runtime::Loader& runtime)
      : ComparisonFilter(config.comparison(), runtime) {}

  // AccessLog::Filter
  bool evaluate(const RequestInfo::RequestInfo& info,
                const Http::HeaderMap& request_headers) override;
};

/**
 * Base operator filter, compose other filters with operation
 */
class OperatorFilter : public Filter {
public:
  OperatorFilter(const Protobuf::RepeatedPtrField<
                     envoy::config::filter::accesslog::v2::AccessLogFilter>& configs,
                 Runtime::Loader& runtime, Runtime::RandomGenerator& random);

protected:
  std::vector<FilterPtr> filters_;
};

/**
 * *And* operator filter, apply logical *and* operation to all of the sub filters.
 */
class AndFilter : public OperatorFilter {
public:
  AndFilter(const envoy::config::filter::accesslog::v2::AndFilter& config, Runtime::Loader& runtime,
            Runtime::RandomGenerator& random);

  // AccessLog::Filter
  bool evaluate(const RequestInfo::RequestInfo& info,
                const Http::HeaderMap& request_headers) override;
};

/**
 * *Or* operator filter, apply logical *or* operation to all of the sub filters.
 */
class OrFilter : public OperatorFilter {
public:
  OrFilter(const envoy::config::filter::accesslog::v2::OrFilter& config, Runtime::Loader& runtime,
           Runtime::RandomGenerator& random);

  // AccessLog::Filter
  bool evaluate(const RequestInfo::RequestInfo& info,
                const Http::HeaderMap& request_headers) override;
};

/**
 * Filter out HC requests.
 */
class NotHealthCheckFilter : public Filter {
public:
  NotHealthCheckFilter() {}

  // AccessLog::Filter
  bool evaluate(const RequestInfo::RequestInfo& info,
                const Http::HeaderMap& request_headers) override;
};

/**
 * Filter traceable requests.
 */
class TraceableRequestFilter : public Filter {
public:
  // AccessLog::Filter
  bool evaluate(const RequestInfo::RequestInfo& info,
                const Http::HeaderMap& request_headers) override;
};

/**
 * Filter that uses a runtime feature key to check if the log should be written.
 */
class RuntimeFilter : public Filter {
public:
  RuntimeFilter(const envoy::config::filter::accesslog::v2::RuntimeFilter& config,
                Runtime::Loader& runtime, Runtime::RandomGenerator& random);

  // AccessLog::Filter
  bool evaluate(const RequestInfo::RequestInfo& info,
                const Http::HeaderMap& request_headers) override;

private:
  Runtime::Loader& runtime_;
  Runtime::RandomGenerator& random_;
  const std::string runtime_key_;
  const envoy::type::FractionalPercent percent_;
  const bool use_independent_randomness_;
};

/**
 * Filter based on headers.
 */
class HeaderFilter : public Filter {
public:
  HeaderFilter(const envoy::config::filter::accesslog::v2::HeaderFilter& config);

  // AccessLog::Filter
  bool evaluate(const RequestInfo::RequestInfo& info,
                const Http::HeaderMap& request_headers) override;

private:
  std::vector<Http::HeaderUtility::HeaderData> header_data_;
};

/**
 * Filter requests that had a response with an Envoy response flag set.
 */
class ResponseFlagFilter : public Filter {
public:
  ResponseFlagFilter(const envoy::config::filter::accesslog::v2::ResponseFlagFilter& config);

  // AccessLog::Filter
  bool evaluate(const RequestInfo::RequestInfo& info,
                const Http::HeaderMap& request_headers) override;

private:
  uint64_t configured_flags_{};
};

/**
 * Access log factory that reads the configuration from proto.
 */
class AccessLogFactory {
public:
  /**
   * Read a filter definition from proto and instantiate an Instance.
   */
  static InstanceSharedPtr fromProto(const envoy::config::filter::accesslog::v2::AccessLog& config,
                                     Server::Configuration::FactoryContext& context);
};

} // namespace AccessLog
} // namespace Envoy
