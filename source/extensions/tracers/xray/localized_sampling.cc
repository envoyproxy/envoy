#include "extensions/tracers/xray/localized_sampling.h"

#include "common/http/exception.h"

#include "extensions/tracers/xray/util.h"

#include "rapidjson/document.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace XRay {

constexpr static double DefaultRate = 0.5;
constexpr static int DefaultFixedTarget = 1;
constexpr static int SamplingFileVersion = 2;
constexpr static char VersionJsonKey[] = "version";
constexpr static char DefaultRuleJsonKey[] = "default";
constexpr static char FixedTargetJsonKey[] = "fixed_target";
constexpr static char RateJsonKey[] = "rate";
constexpr static char CustomRulesJsonKey[] = "rules";
constexpr static char HostJsonKey[] = "host";
constexpr static char HttpMethodJsonKey[] = "http_method";
constexpr static char UrlPathJsonKey[] = "url_path";

static void fail(absl::string_view msg) {
  auto& logger = Logger::Registry::getLog(Logger::Id::tracing);
  ENVOY_LOG_TO_LOGGER(logger, error, "Failed to parse sampling rules - {}", msg);
}

static bool validateRule(const rapidjson::Value& rule) {

  if (rule.HasMember(HostJsonKey) && !rule[HostJsonKey].IsString()) {
    fail("host must be a string");
    return false;
  }

  if (rule.HasMember(HttpMethodJsonKey) && !rule[HttpMethodJsonKey].IsString()) {
    fail("HTTP method must be a string");
    return false;
  }

  if (rule.HasMember(UrlPathJsonKey) && !rule[UrlPathJsonKey].IsString()) {
    fail("URL path must be a string");
    return false;
  }

  if (!rule.HasMember(FixedTargetJsonKey) || !rule[FixedTargetJsonKey].IsNumber() ||
      rule[FixedTargetJsonKey].GetInt() < 0) {
    fail("fixed target is missing or not a valid positive integer");
    return false;
  }

  if (!rule.HasMember(RateJsonKey) || !rule[RateJsonKey].IsNumber() ||
      rule[RateJsonKey].GetDouble() < 0) {
    fail("rate is missing or not a valid positive floating number");
    return false;
  }
  return true;
}

LocalizedSamplingRule LocalizedSamplingRule::createDefault() {
  return LocalizedSamplingRule(DefaultFixedTarget, DefaultRate);
}

bool LocalizedSamplingRule::appliesTo(const SamplingRequest& request) const {
  return (request.host.empty() || wildcardMatch(host_, request.host)) &&
         (request.http_method.empty() || wildcardMatch(http_method_, request.http_method)) &&
         (request.http_url.empty() || wildcardMatch(url_path_, request.http_url));
}

LocalizedSamplingManifest::LocalizedSamplingManifest(const std::string& rule_json)
    : default_rule_(LocalizedSamplingRule::createDefault()) {
  if (rule_json.empty()) {
    return;
  }

  rapidjson::Document document;
  document.Parse(rule_json);
  if (document.HasParseError()) {
    fail("invalid JSON format.");
    return;
  }

  if (!document.HasMember(VersionJsonKey)) {
    fail("missing version number");
    return;
  }

  if (!document[VersionJsonKey].IsNumber() ||
      document[VersionJsonKey].GetInt() != SamplingFileVersion) {
    fail("wrong version number");
    return;
  }

  if (!document.HasMember(DefaultRuleJsonKey) || !document[DefaultRuleJsonKey].IsObject()) {
    fail("missing default rule");
    return;
  }

  // extract default rule members
  const auto& default_rule_object = document[DefaultRuleJsonKey];
  if (!validateRule(default_rule_object)) {
    return;
  }

  default_rule_.setRate(default_rule_object[RateJsonKey].GetDouble());
  default_rule_.setFixedTarget(default_rule_object[FixedTargetJsonKey].GetInt());

  if (!document.HasMember(CustomRulesJsonKey)) {
    return;
  }

  if (!document[CustomRulesJsonKey].IsArray()) {
    fail("rules must be JSON array");
    return;
  }

  for (auto& rule_json : document[CustomRulesJsonKey].GetArray()) {
    if (!validateRule(rule_json)) {
      return;
    }

    LocalizedSamplingRule rule = LocalizedSamplingRule::createDefault();
    if (rule_json.HasMember(HostJsonKey)) {
      rule.setHost(rule_json[HostJsonKey].GetString());
    }

    if (rule_json.HasMember(HttpMethodJsonKey)) {
      rule.setHttpMethod(rule_json[HttpMethodJsonKey].GetString());
    }

    if (rule_json.HasMember(UrlPathJsonKey)) {
      rule.setUrlPath(rule_json[UrlPathJsonKey].GetString());
    }

    // rate and fixed_target must exist because we validated this rule
    rule.setRate(rule_json[RateJsonKey].GetDouble());
    rule.setFixedTarget(rule_json[FixedTargetJsonKey].GetUint());

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
  const auto now = time_source_.get().monotonicTime();
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
