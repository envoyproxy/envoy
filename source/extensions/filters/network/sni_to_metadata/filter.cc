#include "source/extensions/filters/network/sni_to_metadata/filter.h"

#include <string>

#include "source/common/protobuf/protobuf.h"
#include "source/common/common/regex.h"
#include "source/common/common/utility.h"

#include "envoy/common/exception.h"

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace SniToMetadata {

Config::Config(
    const FilterConfig& config,
    Regex::Engine& regex_engine,
    absl::Status& creation_status) {
  // Compile all connection rules
  for (const auto& rule_config : config.connection_rules()) {
    CompiledConnectionRule compiled_rule;

    // Compile the regex pattern if one is specified
    if (rule_config.has_pattern()) {
      auto regex_result = Regex::Utility::parseRegex(rule_config.pattern(), regex_engine);
      SET_AND_RETURN_IF_NOT_OK(regex_result.status(), creation_status);
      compiled_rule.regex_matcher = std::move(regex_result.value());
    }
    // If no pattern is specified, regex_matcher remains nullptr

    for (const auto& target : rule_config.metadata_targets()) {
      compiled_rule.metadata_targets.push_back(target);
    }

    compiled_rules_.push_back(std::move(compiled_rule));
  }
}

Filter::Filter(ConfigSharedPtr config) : config_(std::move(config)), initialized_(false) {}

Network::FilterStatus Filter::onData(Buffer::Instance&, bool) {
  // Only process once per connection
  if (initialized_) {
    return Network::FilterStatus::Continue;
  }

  initialized_ = true;

  absl::string_view sni = callbacks_->connection().requestedServerName();

  if (sni.empty()) {
    ENVOY_CONN_LOG(trace, "sni_to_metadata: no SNI found", callbacks_->connection());
    return Network::FilterStatus::Continue;
  }

  ENVOY_CONN_LOG(trace, "sni_to_metadata: processing SNI {}", callbacks_->connection(), sni);

  bool any_rule_matched = processConnectionRules(sni);

  if (!any_rule_matched) {
    ENVOY_CONN_LOG(debug, "sni_to_metadata: no rules matched SNI {}", callbacks_->connection(),
                   sni);
  }

  return Network::FilterStatus::Continue;
}

bool Filter::processConnectionRules(absl::string_view sni) {
  bool any_matched = false;

  // Evaluate rules in order, stop at first match
  for (const auto& rule : config_->compiledRules()) {
    if (applyConnectionRule(rule, sni)) {
      any_matched = true;
      break;
    }
  }

  return any_matched;
}

bool Filter::applyConnectionRule(const CompiledConnectionRule& rule, absl::string_view sni) {
  // Check if rule matches
  if (rule.regex_matcher != nullptr) {
    // Rule has a regex pattern - check if it matches
    if (!rule.regex_matcher->match(sni)) {
      return false;
    }
    ENVOY_CONN_LOG(trace, "sni_to_metadata: regex rule matched SNI {}", callbacks_->connection(),
                   sni);
  } else {
    // Rule has no pattern - always matches
    ENVOY_CONN_LOG(trace, "sni_to_metadata: pattern-less rule applied to SNI {}",
                   callbacks_->connection(), sni);
  }

  // Apply metadata targets for this rule
  bool metadata_applied = false;
  for (const auto& target : rule.metadata_targets) {
    std::string metadata_value;

    if (target.metadata_value().empty()) {
      // Use the full SNI if no specific value is configured
      metadata_value = std::string(sni);
    } else if (rule.regex_matcher != nullptr) {
      // Apply regex substitution using capture groups
      metadata_value = rule.regex_matcher->replaceAll(sni, target.metadata_value());
    } else {
      // No regex pattern - use metadata_value as-is (no substitution)
      metadata_value = target.metadata_value();
    }

    if (!metadata_value.empty()) {
      absl::string_view effective_namespace = target.metadata_namespace().empty()
                                                  ? DEFAULT_METADATA_NAMESPACE
                                                  : target.metadata_namespace();

      // Store in dynamic metadata
      Protobuf::Struct& metadata = (*callbacks_->connection()
                                            .streamInfo()
                                            .dynamicMetadata()
                                            .mutable_filter_metadata())[effective_namespace];
      (*metadata.mutable_fields())[target.metadata_key()].set_string_value(std::move(metadata_value));

      metadata_applied = true;
    }
  }

  return metadata_applied;
}

} // namespace SniToMetadata
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy