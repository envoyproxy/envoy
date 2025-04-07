#pragma once

#include <memory>

#include "envoy/extensions/common/matching/v3/extension_matcher.pb.validate.h"
#include "envoy/extensions/filters/common/matcher/action/v3/skip_action.pb.h"
#include "envoy/matcher/matcher.h"
#include "envoy/network/listen_socket.h"
#include "envoy/server/filter_config.h"

#include "source/common/matcher/matcher.h"
#include "source/common/network/matching/data_impl.h"
#include "source/extensions/filters/network/common/factory_base.h"
#include "source/extensions/filters/network/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MatchDelegate {

/**
 * Action that allows a filter to be skipped based on match conditions.
 * This is used to conditionally bypass filter processing when certain criteria are met.
 */
class SkipAction : public Matcher::ActionBase<
                       envoy::extensions::filters::common::matcher::action::v3::SkipFilter> {};

using NetworkFilterActionContext = Server::Configuration::ServerFactoryContext;

/**
 * A network filter that delegates filter operations to wrapped filters based on match conditions.
 * This filter can dynamically choose to apply or skip the wrapped filter based on match criteria.
 */
class DelegatingNetworkFilter : protected Logger::Loggable<Logger::Id::filter>,
                                public Envoy::Network::Filter {
public:
  /**
   * Helper class that manages the matching state for a delegating filter.
   * Tracks whether the wrapped filter should be skipped based on match evaluation.
   */
  class FilterMatchState {
  public:
    FilterMatchState(Matcher::MatchTreeSharedPtr<Envoy::Network::MatchingData> match_tree)
        : match_tree_(std::move(match_tree)), has_match_tree_(match_tree_ != nullptr) {}

    void evaluateMatchTree();
    bool skipFilter() const { return skip_filter_; }
    void onConnection(const Envoy::Network::ConnectionSocket& socket,
                      const StreamInfo::StreamInfo& stream_info) {
      if (matching_data_ == nullptr) {
        matching_data_ = std::make_shared<Envoy::Network::Matching::MatchingDataImpl>(
            socket, stream_info.filterState(), stream_info.dynamicMetadata());
      }
    }

  private:
    Matcher::MatchTreeSharedPtr<Envoy::Network::MatchingData> match_tree_;
    bool has_match_tree_{};

    Envoy::Network::Matching::NetworkMatchingDataImplSharedPtr matching_data_;
    bool match_tree_evaluated_{};
    bool skip_filter_{};
  };

  /**
   * Constructor.
   *
   * @param match_tree The match tree to evaluate for determining if the filter should be skipped
   * @param read_filter The read filter to be conditionally applied
   * @param write_filter The write filter to be conditionally applied
   */
  DelegatingNetworkFilter(Matcher::MatchTreeSharedPtr<Envoy::Network::MatchingData> match_tree,
                          Envoy::Network::ReadFilterSharedPtr read_filter,
                          Envoy::Network::WriteFilterSharedPtr write_filter);

  // Envoy::Network::ReadFilter
  Envoy::Network::FilterStatus onNewConnection() override;
  Envoy::Network::FilterStatus onData(Buffer::Instance& data, bool end_stream) override;
  void initializeReadFilterCallbacks(Envoy::Network::ReadFilterCallbacks& callbacks) override;

  // Envoy::Network::WriteFilter
  Envoy::Network::FilterStatus onWrite(Buffer::Instance& data, bool end_stream) override;
  void initializeWriteFilterCallbacks(Envoy::Network::WriteFilterCallbacks& callbacks) override;

private:
  FilterMatchState match_state_;

  Envoy::Network::ReadFilterSharedPtr read_filter_;
  Envoy::Network::WriteFilterSharedPtr write_filter_;
  Envoy::Network::ReadFilterCallbacks* read_callbacks_{};
  Envoy::Network::WriteFilterCallbacks* write_callbacks_{};
};

/**
 * Configuration for the delegating network filter.
 * Creates filter instances based on configuration proto.
 */
class MatchDelegateConfig : public Extensions::NetworkFilters::Common::FactoryBase<
                                envoy::extensions::common::matching::v3::ExtensionWithMatcher> {
public:
  MatchDelegateConfig() : FactoryBase(NetworkFilterNames::get().NetworkMatchDelegate) {}

private:
  Envoy::Network::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::extensions::common::matching::v3::ExtensionWithMatcher& proto_config,
      Server::Configuration::FactoryContext& context) override;

  Envoy::Network::FilterFactoryCb createFilterFactory(
      const envoy::extensions::common::matching::v3::ExtensionWithMatcher& proto_config,
      ProtobufMessage::ValidationVisitor& validation, NetworkFilterActionContext& action_context,
      Server::Configuration::FactoryContext& context,
      Server::Configuration::NamedNetworkFilterConfigFactory& factory);
};

DECLARE_FACTORY(MatchDelegateConfig);

namespace Factory {

/**
 * FilterManager implementation that wraps the provided FilterManager and
 * wraps any filters with a DelegatingNetworkFilter.
 */
class DelegatingNetworkFilterManager : public Envoy::Network::FilterManager {
public:
  /**
   * @param filter_manager The underlying filter manager to delegate to
   * @param match_tree The match tree to use for filter delegation decisions
   */
  DelegatingNetworkFilterManager(
      Envoy::Network::FilterManager& filter_manager,
      Matcher::MatchTreeSharedPtr<Envoy::Network::MatchingData> match_tree);
  ~DelegatingNetworkFilterManager() override = default;

  // Network::FilterManager
  void addReadFilter(Envoy::Network::ReadFilterSharedPtr filter) override;
  void addWriteFilter(Envoy::Network::WriteFilterSharedPtr filter) override;
  void addFilter(Envoy::Network::FilterSharedPtr filter) override;
  void removeReadFilter(Envoy::Network::ReadFilterSharedPtr filter) override;
  bool initializeReadFilters() override;

private:
  Envoy::Network::FilterManager& filter_manager_;
  Matcher::MatchTreeSharedPtr<Envoy::Network::MatchingData> match_tree_;
};

DECLARE_FACTORY(SkipActionFactory);

} // namespace Factory

} // namespace MatchDelegate
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
