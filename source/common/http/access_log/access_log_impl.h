#pragma once

#include "envoy/access_log/access_log.h"
#include "envoy/http/access_log.h"
#include "envoy/runtime/runtime.h"

#include "common/json/json_loader.h"

namespace Http {
namespace AccessLog {

/**
 * Type of filter comparison operation to perform.
 */
enum class FilterOperation { GreaterEqual, Equal };

/**
 * Base implementation of an access log filter that reads from JSON.
 */
class FilterImpl : public Filter {
public:
  /**
   * Read a filter definition from JSON and instantiate a concrete filter class.
   */
  static FilterPtr fromJson(Json::Object& json, Runtime::Loader& runtime);

protected:
  FilterImpl(Json::Object& json, Runtime::Loader& runtime);

  bool compareAgainstValue(uint64_t lhs);

  FilterOperation op_;
  uint64_t value_;
  Runtime::Loader& runtime_;
  Optional<std::string> runtime_key_;
};

/**
 * Filter on response status code.
 */
class StatusCodeFilter : public FilterImpl {
public:
  StatusCodeFilter(Json::Object& json, Runtime::Loader& runtime) : FilterImpl(json, runtime) {}

  // Http::AccessLog::Filter
  bool evaluate(const RequestInfo& info, const HeaderMap& request_headers) override;
};

/**
 * Filter on total request/response duration.
 */
class DurationFilter : public FilterImpl {
public:
  DurationFilter(Json::Object& json, Runtime::Loader& runtime) : FilterImpl(json, runtime) {}

  // Http::AccessLog::Filter
  bool evaluate(const RequestInfo& info, const HeaderMap& request_headers) override;
};

/**
 * Base operator filter, compose other filters with operation
 */
class OperatorFilter : public Filter {
public:
  OperatorFilter(const Json::Object& json, Runtime::Loader& runtime);

protected:
  std::vector<FilterPtr> filters_;
};

/**
 * *And* operator filter, apply logical *and* operation to all of the sub filters.
 */
class AndFilter : public OperatorFilter {
public:
  AndFilter(const Json::Object& json, Runtime::Loader& runtime);

  // Http::AccessLog::Filter
  bool evaluate(const RequestInfo& info, const HeaderMap& request_headers) override;
};

/**
 * *Or* operator filter, apply logical *or* operation to all of the sub filters.
 */
class OrFilter : public OperatorFilter {
public:
  OrFilter(const Json::Object& json, Runtime::Loader& runtime);

  // Http::AccessLog::Filter
  bool evaluate(const RequestInfo& info, const HeaderMap& request_headers) override;
};

/**
 * Filter out HC requests.
 */
class NotHealthCheckFilter : public Filter {
public:
  NotHealthCheckFilter() {}

  // Http::AccessLog::Filter
  bool evaluate(const RequestInfo& info, const HeaderMap& request_headers) override;
};

/**
 * Filter traceable requests.
 */
class TraceableRequestFilter : public Filter {
public:
  TraceableRequestFilter(Runtime::Loader& runtime);

  // Http::AccessLog::Filter
  bool evaluate(const RequestInfo& info, const HeaderMap& request_headers) override;

private:
  Runtime::Loader& runtime_;
};

/**
 * Filter that uses a runtime feature key to check if the log should be written.
 */
class RuntimeFilter : public Filter {
public:
  RuntimeFilter(Json::Object& json, Runtime::Loader& runtime);

  // Http::AccessLog::Filter
  bool evaluate(const RequestInfo& info, const HeaderMap& request_headers) override;

private:
  Runtime::Loader& runtime_;
  const std::string runtime_key_;
};

class InstanceImpl : public Instance {
public:
  InstanceImpl(const std::string& access_log_path, FilterPtr&& filter, FormatterPtr&& formatter,
               ::AccessLog::AccessLogManager& log_manager);

  static InstancePtr fromJson(Json::Object& json, Runtime::Loader& runtime,
                              ::AccessLog::AccessLogManager& log_manager);

  // Http::AccessLog::Instance
  void log(const HeaderMap* request_headers, const HeaderMap* response_headers,
           const RequestInfo& request_info) override;

private:
  Filesystem::FilePtr log_file_;
  FilterPtr filter_;
  FormatterPtr formatter_;
};

} // AccessLog
} // Http
