#include "extensions/tracers/xray/localized_sampling.h"

#include "common/http/exception.h"
#include "common/protobuf/utility.h"

#include "extensions/tracers/xray/util.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace XRay {

constexpr double DefaultRate = 0.5;
constexpr int DefaultFixedTarget = 1;
constexpr int SamplingFileVersion = 2;
constexpr auto VersionJsonKey = "version";
constexpr auto DefaultRuleJsonKey = "default";
constexpr auto FixedTargetJsonKey = "fixed_target";
constexpr auto RateJsonKey = "rate";
constexpr auto CustomRulesJsonKey = "rules";
constexpr auto HostJsonKey = "host";
constexpr auto HttpMethodJsonKey = "http_method";
constexpr auto UrlPathJsonKey = "url_path";

namespace {
void fail(absl::string_view msg) {
  auto& logger = Logger::Registry::getLog(Logger::Id::tracing);
  ENVOY_LOG_TO_LOGGER(logger, error, "Failed to parse sampling rules - {}", msg);
}

bool is_valid_rate(double n) { return n >= 0 && n <= 1.0; }
bool is_valid_fixed_target(double n) { return n >= 0 && static_cast<uint32_t>(n) == n; }

bool validateRule(const ProtobufWkt::Struct& rule) {
  using ProtobufWkt::Value;

  const auto host_it = rule.fields().find(HostJsonKey);
  if (host_it != rule.fields().end() &&
      host_it->second.kind_case() != Value::KindCase::kStringValue) {
    fail("host must be a string");
    return false;
  }

  const auto http_method_it = rule.fields().find(HttpMethodJsonKey);
  if (http_method_it != rule.fields().end() &&
      http_method_it->second.kind_case() != Value::KindCase::kStringValue) {
    fail("HTTP method must be a string");
    return false;
  }

  const auto url_path_it = rule.fields().find(UrlPathJsonKey);
  if (url_path_it != rule.fields().end() &&
      url_path_it->second.kind_case() != Value::KindCase::kStringValue) {
    fail("URL path must be a string");
    return false;
  }

  const auto fixed_target_it = rule.fields().find(FixedTargetJsonKey);
  if (fixed_target_it == rule.fields().end() ||
      fixed_target_it->second.kind_case() != Value::KindCase::kNumberValue ||
      !is_valid_fixed_target(fixed_target_it->second.number_value())) {
    fail("fixed target is missing or not a valid positive integer");
    return false;
  }

  const auto rate_it = rule.fields().find(RateJsonKey);
  if (rate_it == rule.fields().end() ||
      rate_it->second.kind_case() != Value::KindCase::kNumberValue ||
      !is_valid_rate(rate_it->second.number_value())) {
    fail("rate is missing or not a valid positive floating number");
    return false;
  }
  return true;
}
} // namespace

LocalizedSamplingRule LocalizedSamplingRule::createDefault() {
  return LocalizedSamplingRule(DefaultFixedTarget, DefaultRate);
}

bool LocalizedSamplingRule::appliesTo(const SamplingRequest& request) const {
  return (request.host_.empty() || wildcardMatch(host_, request.host_)) &&
         (request.http_method_.empty() || wildcardMatch(http_method_, request.http_method_)) &&
         (request.http_url_.empty() || wildcardMatch(url_path_, request.http_url_));
}

LocalizedSamplingManifest::LocalizedSamplingManifest(const std::string& rule_json)
    : default_rule_(LocalizedSamplingRule::createDefault()) {
  if (rule_json.empty()) {
    return;
  }

  ProtobufWkt::Struct document;
  try {
    MessageUtil::loadFromJson(rule_json, document);
  } catch (EnvoyException& e) {
    fail("invalid JSON format");
    return;
  }

  const auto version_it = document.fields().find(VersionJsonKey);
  if (version_it == document.fields().end()) {
    fail("missing version number");
    return;
  }

  if (version_it->second.kind_case() != ProtobufWkt::Value::KindCase::kNumberValue ||
      version_it->second.number_value() != SamplingFileVersion) {
    fail("wrong version number");
    return;
  }

  const auto default_rule_it = document.fields().find(DefaultRuleJsonKey);
  if (default_rule_it == document.fields().end() ||
      default_rule_it->second.kind_case() != ProtobufWkt::Value::KindCase::kStructValue) {
    fail("missing default rule");
    return;
  }

  // extract default rule members
  auto& default_rule_object = default_rule_it->second.struct_value();
  if (!validateRule(default_rule_object)) {
    return;
  }

  default_rule_.setRate(default_rule_object.fields().find(RateJsonKey)->second.number_value());
  default_rule_.setFixedTarget(static_cast<uint32_t>(
      default_rule_object.fields().find(FixedTargetJsonKey)->second.number_value()));

  const auto custom_rules_it = document.fields().find(CustomRulesJsonKey);
  if (custom_rules_it == document.fields().end()) {
    return;
  }

  if (custom_rules_it->second.kind_case() != ProtobufWkt::Value::KindCase::kListValue) {
    fail("rules must be JSON array");
    return;
  }

  for (auto& el : custom_rules_it->second.list_value().values()) {
    if (el.kind_case() != ProtobufWkt::Value::KindCase::kStructValue) {
      fail("rules array must be objects");
      return;
    }

    auto& rule_json = el.struct_value();
    if (!validateRule(rule_json)) {
      return;
    }

    LocalizedSamplingRule rule = LocalizedSamplingRule::createDefault();
    const auto host_it = rule_json.fields().find(HostJsonKey);
    if (host_it != rule_json.fields().end()) {
      rule.setHost(host_it->second.string_value());
    }

    const auto http_method_it = rule_json.fields().find(HttpMethodJsonKey);
    if (http_method_it != rule_json.fields().end()) {
      rule.setHttpMethod(http_method_it->second.string_value());
    }

    const auto url_path_it = rule_json.fields().find(UrlPathJsonKey);
    if (url_path_it != rule_json.fields().end()) {
      rule.setUrlPath(url_path_it->second.string_value());
    }

    // rate and fixed_target must exist because we validated this rule
    rule.setRate(rule_json.fields().find(RateJsonKey)->second.number_value());
    rule.setFixedTarget(
        static_cast<uint32_t>(rule_json.fields().find(FixedTargetJsonKey)->second.number_value()));

    custom_rules_.push_back(std::move(rule));
  }
}

bool LocalizedSamplingStrategy::shouldTrace(const SamplingRequest& sampling_request) {
  if (!custom_manifest_.hasCustomRules()) {
    return shouldTrace(default_manifest_.defaultRule());
  }

  for (auto&& rule : custom_manifest_.customRules()) {
    if (rule.appliesTo(sampling_request)) {
      return shouldTrace(rule);
    }
  }
  return shouldTrace(custom_manifest_.defaultRule());
}

bool LocalizedSamplingStrategy::shouldTrace(LocalizedSamplingRule& rule) {
  const auto now = time_source_.monotonicTime();
  if (rule.reservoir().take(now)) {
    return true;
  }

  // rule.rate() is a rational number between 0 and 1
  auto toss = random() % 100;
  if (toss < (100 * rule.rate())) {
    return true;
  }

  return false;
}

} // namespace XRay
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
