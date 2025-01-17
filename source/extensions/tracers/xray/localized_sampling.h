#pragma once

#include <functional>
#include <string>
#include <vector>

#include "envoy/common/time.h"

#include "source/common/common/logger.h"
#include "source/common/common/random_generator.h"
#include "source/extensions/tracers/xray/reservoir.h"
#include "source/extensions/tracers/xray/sampling_strategy.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace XRay {

/**
 * This class encompasses the algorithm used when deciding whether to sample a given request.
 * The rule contains wildcard strings for matching a request based on its hostname, HTTP method or
 * URL path. A request must match on all 3 parts before this rule is applied.
 * If the rule applies, then |fixed_target| determines how many requests to sample per second.
 * While, rate determines the percentage of requests to sample after that within the same second.
 *
 * By default, this tracer records the first request each second, and five percent of
 * any additional requests.
 */
class LocalizedSamplingRule {
public:
  /**
   * Creates a default sampling rule that has the default |fixed_target| and default |rate| set.
   */
  static LocalizedSamplingRule createDefault();

  LocalizedSamplingRule(uint32_t fixed_target, double rate)
      : fixed_target_(fixed_target), rate_(rate), reservoir_(fixed_target_) {}

  /**
   * Determines whether Hostname, HTTP method and URL path match the given request.
   */
  bool appliesTo(const SamplingRequest& request) const;

  /**
   * Set the hostname to match against.
   * This value can contain wildcard characters such as '*' or '?'.
   */
  void setHost(absl::string_view host) { host_ = std::string(host); }

  /**
   * Set the HTTP method to match against.
   * This value can contain wildcard characters such as '*' or '?'.
   */
  void setHttpMethod(absl::string_view http_method) { http_method_ = std::string(http_method); }

  /**
   * Set the URL path to match against.
   * This value can contain wildcard characters such as '*' or '?'.
   */
  void setUrlPath(absl::string_view url_path) { url_path_ = std::string(url_path); }

  /**
   * Set the minimum number of requests to sample per second.
   */
  void setFixedTarget(uint32_t fixed_target) {
    fixed_target_ = fixed_target;
    reservoir_ = Reservoir(fixed_target);
  }

  /**
   * Set the percentage of requests to sample _after_ sampling |fixed_target| requests per second.
   */
  void setRate(double rate) { rate_ = rate; }
  double rate() const { return rate_; }
  Reservoir& reservoir() { return reservoir_; }

private:
  std::string host_;
  std::string http_method_;
  std::string url_path_;
  uint32_t fixed_target_;
  double rate_;
  Reservoir reservoir_;
};

/**
 * The manifest represents the set of sampling rules (custom and default) used to match incoming
 * requests.
 */
class LocalizedSamplingManifest {
public:
  /**
   * Create a default manifest. The default manifest is used when a custom manifest does not exist
   * or failed to parse. The default manifest, will have an empty set of custom rules.
   */
  static LocalizedSamplingManifest createDefault() {
    return LocalizedSamplingManifest{LocalizedSamplingRule::createDefault()};
  }

  /**
   * Create a manifest by de-serializing the input string as JSON representation of the sampling
   * rules.
   * @param sampling_rules_json JSON representation of X-Ray localized sampling rules.
   */
  explicit LocalizedSamplingManifest(const std::string& sampling_rules_json);

  /**
   * Create a manifest by assigning the argument rule as the default rule. The set of custom rules
   * in this manifest will be empty.
   * @param default_rule A localized sampling rule that will be assigned as the default rule.
   */
  explicit LocalizedSamplingManifest(const LocalizedSamplingRule& default_rule)
      : default_rule_(default_rule) {}

  /**
   * @return default sampling rule
   */
  LocalizedSamplingRule& defaultRule() { return default_rule_; }

  /**
   * @return the user-defined sampling rules
   */
  std::vector<LocalizedSamplingRule>& customRules() { return custom_rules_; }

  /**
   * @return true if this manifest has a set of custom rules; otherwise false.
   */
  bool hasCustomRules() const { return !custom_rules_.empty(); }

private:
  LocalizedSamplingRule default_rule_;
  std::vector<LocalizedSamplingRule> custom_rules_;
};

class LocalizedSamplingStrategy : public SamplingStrategy {
public:
  LocalizedSamplingStrategy(const std::string& sampling_rules_json, Random::RandomGenerator& rng,
                            TimeSource& time_source)
      : SamplingStrategy(rng), manifest_(sampling_rules_json), time_source_(time_source) {}

  /**
   * Determines if an incoming request matches one of the sampling rules in the local manifest.
   * If a match is found, then the request might be traced based on the sampling percentages etc.
   * determined by the matching rule.
   */
  bool shouldTrace(const SamplingRequest& sampling_request) override;

  const LocalizedSamplingManifest manifest() const { return manifest_; }

private:
  bool shouldTrace(LocalizedSamplingRule& rule);
  LocalizedSamplingManifest manifest_;
  TimeSource& time_source_;
};

} // namespace XRay
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
