#pragma once

#include <memory>
#include <vector>

#include "envoy/buffer/buffer.h"
#include "envoy/extensions/filters/network/sni_to_metadata/v3/sni_to_metadata.pb.h"
#include "envoy/network/filter.h"

#include "source/common/common/logger.h"
#include "source/common/common/regex.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace SniToMetadata {

using FilterConfig = envoy::extensions::filters::network::sni_to_metadata::v3::SniToMetadataFilter;

/**
 * Represents a compiled connection rule with optional regex matcher and metadata targets.
 */
struct CompiledConnectionRule {
  // Compiled regex pattern for SNI matching (nullptr if no pattern specified)
  Regex::CompiledMatcherPtr regex_matcher;

  // List of metadata targets to populate when this rule matches
  std::vector<FilterConfig::MetadataTarget> metadata_targets;
};

/**
 * Configuration for the SniToMetadata filter.
 */
class Config {
public:
  Config(const FilterConfig& config, Regex::Engine& regex_engine, absl::Status& creation_status);

  const std::vector<CompiledConnectionRule>& compiledRules() const { return compiled_rules_; }

private:
  // Compiled connection rules ready for processing
  std::vector<CompiledConnectionRule> compiled_rules_;
};

using ConfigSharedPtr = std::shared_ptr<Config>;

/**
 * A network filter that extracts SNI from the connection, applies regex patterns with capture
 * groups, and stores the results into dynamic metadata according to the configured rules.
 */
class Filter : public Network::ReadFilter, Logger::Loggable<Logger::Id::filter> {
public:
  Filter(ConfigSharedPtr config);

  // Network::ReadFilter
  void initializeReadFilterCallbacks(Network::ReadFilterCallbacks& callbacks) override {
    callbacks_ = &callbacks;
  }
  Network::FilterStatus onData(Buffer::Instance&, bool) override;
  Network::FilterStatus onNewConnection() override { return Network::FilterStatus::Continue; }

private:
  /**
   * Process SNI against all connection rules and apply matching metadata.
   * Returns true if any rule matched and metadata was applied.
   */
  bool processConnectionRules(absl::string_view sni);

  /**
   * Apply a specific connection rule to the SNI value.
   * Returns true if the rule matched and metadata was applied.
   */
  bool applyConnectionRule(const CompiledConnectionRule& rule, absl::string_view sni);

  ConfigSharedPtr config_;
  Network::ReadFilterCallbacks* callbacks_;
  bool initialized_ : 1;

  // Default metadata namespace
  static constexpr absl::string_view DEFAULT_METADATA_NAMESPACE =
      "envoy.filters.network.sni_to_metadata";
};

} // namespace SniToMetadata
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
