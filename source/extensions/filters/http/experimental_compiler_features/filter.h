#pragma once

#include <string>

#include "envoy/extensions/filters/http/experimental_compiler_features/v3/experimental_compiler_features.pb.h"
#include "envoy/http/filter.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"

#include "extensions/filters/http/common/pass_through_filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExperimentalCompilerFeatures {

/**
 * Abstract filter configuration.
 */
class FilterConfig {
public:
  virtual ~FilterConfig() = default;

  virtual const std::string& key() const PURE;
  virtual const std::string& val() const PURE;

  virtual bool associativeContainerUseContains() const PURE;
  virtual bool enumMembersInScope() const PURE;
  virtual bool strStartsWith() const PURE;
  virtual bool strEndsWith() const PURE;

  // These vars are used for testing only.
  virtual const std::string& enumValue() const PURE;
  virtual const std::string& startEndString() const PURE;
  virtual const std::string& associativeContainerString() const PURE;
};

using FilterConfigSharedPtr = std::shared_ptr<FilterConfig>;

/**
 * Configuration for the Experimental Compiler Features filter.
 */
class FilterConfigImpl : public FilterConfig {
public:
  FilterConfigImpl(const std::string& key, const std::string& value,
                   bool associative_container_use_contains,
                   bool enum_members_in_scope, bool str_starts_with,
                   bool str_ends_with, const std::string& enum_value,
                   const std::string& start_end_string,
                   const std::string& associative_container_string);

  // Basic key-value header variables
  const std::string& key() const override;
  const std::string& val() const override;

  // Variables to enable C++20 features
  bool associativeContainerUseContains() const override;
  bool enumMembersInScope() const override;
  bool strStartsWith() const override;
  bool strEndsWith() const override;

  // These vars are used for testing only.
  const std::string& enumValue() const override;
  const std::string& startEndString() const override;
  const std::string& associativeContainerString() const override;

private:
  std::string key_;
  std::string val_;

  bool associative_container_use_contains_;
  bool enum_members_in_scope_;
  bool str_starts_with_;
  bool str_ends_with_;

  // These vars are used for testing only.
  std::string enum_value_;
  std::string start_end_string_;
  std::string associative_container_string_;
};

/**
 * Experimental Compiler Features auth filter.
 */
class Filter : public Http::PassThroughDecoderFilter, Logger::Loggable<Logger::Id::filter> {
public:
  Filter(const std::shared_ptr<FilterConfig>& config);

  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap& headers,
                                          bool end_stream) override;

private:
  friend class FilterTest;
  // This var is used for testing only.
  std::string enum_value_{"foo"};

  std::shared_ptr<FilterConfig> config_;
  const Http::LowerCaseString headerKey() const;
  const std::string headerValue() const;
};

} // namespace ExperimentalCompilerFeatures
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
