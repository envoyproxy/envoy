#pragma once

#include "envoy/extensions/filters/common/set_filter_state/v3/value.pb.h"
#include "envoy/formatter/substitution_formatter.h"
#include "envoy/stream_info/filter_state.h"

#include "source/common/common/logger.h"
#include "source/common/protobuf/protobuf.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace SetFilterState {

using LifeSpan = StreamInfo::FilterState::LifeSpan;
using StateType = StreamInfo::FilterState::StateType;
using StreamSharing = StreamInfo::StreamSharingMayImpactPooling;
using FilterStateValueProto =
    envoy::extensions::filters::common::set_filter_state::v3::FilterStateValue;

struct Value {
  std::string key_;
  const StreamInfo::FilterState::ObjectFactory* factory_;
  StateType state_type_{StateType::ReadOnly};
  StreamSharing stream_sharing_{StreamSharing::None};
  bool skip_if_empty_;
  Formatter::FormatterConstSharedPtr value_;
};

class Config : public ::Envoy::Router::RouteSpecificFilterConfig,
               public Logger::Loggable<Logger::Id::config> {
public:
  Config(const Protobuf::RepeatedPtrField<FilterStateValueProto>& proto_values, LifeSpan life_span,
         Server::Configuration::GenericFactoryContext& context)
      : Config(proto_values, life_span, context, false) {}
  Config(const Protobuf::RepeatedPtrField<FilterStateValueProto>& proto_values, LifeSpan life_span,
         Server::Configuration::GenericFactoryContext& context, bool clear_route_cache)
      : life_span_(life_span), values_(parse(proto_values, context)),
        clear_route_cache_(clear_route_cache) {}
  void updateFilterState(const Formatter::Context& context, StreamInfo::StreamInfo& info) const;
  bool clearRouteCache() { return clear_route_cache_; };

private:
  std::vector<Value> parse(const Protobuf::RepeatedPtrField<FilterStateValueProto>& proto_values,
                           Server::Configuration::GenericFactoryContext& context) const;
  const LifeSpan life_span_;
  const std::vector<Value> values_;
  const bool clear_route_cache_{false};
};

using ConfigSharedPtr = std::shared_ptr<Config>;

} // namespace SetFilterState
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
