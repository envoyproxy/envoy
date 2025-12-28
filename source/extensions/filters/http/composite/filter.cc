#include "source/extensions/filters/http/composite/filter.h"

#include "envoy/http/filter.h"

#include "source/common/common/stl_helpers.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Composite {

namespace {
// Helper that returns `filter->func(args...)` if the filter is not null, returning `rval`
// otherwise.
template <class FilterPtrT, class FuncT, class RValT, class... Args>
RValT delegateFilterActionOr(FilterPtrT& filter, FuncT func, RValT rval, Args&&... args) {
  if (filter) {
    return ((*filter).*func)(std::forward<Args>(args)...);
  }

  return rval;
}

// Own version of lambda overloading since std::overloaded is not available to use yet.
template <class... Ts> struct Overloaded : Ts... {
  using Ts::operator()...;
};

template <class... Ts> Overloaded(Ts...) -> Overloaded<Ts...>;

} // namespace

std::unique_ptr<Protobuf::Struct> MatchedActionInfo::buildProtoStruct() const {
  auto message = std::make_unique<Protobuf::Struct>();
  auto& fields = *message->mutable_fields();
  for (const auto& p : actions_) {
    fields[p.first] = ValueUtil::stringValue(p.second);
  }
  return message;
}

Http::FilterHeadersStatus Filter::decodeHeaders(Http::RequestHeaderMap& headers, bool end_stream) {
  return delegateFilterActionOr(delegated_filter_, &StreamDecoderFilter::decodeHeaders,
                                Http::FilterHeadersStatus::Continue, headers, end_stream);
}

Http::FilterDataStatus Filter::decodeData(Buffer::Instance& data, bool end_stream) {
  return delegateFilterActionOr(delegated_filter_, &StreamDecoderFilter::decodeData,
                                Http::FilterDataStatus::Continue, data, end_stream);
}

Http::FilterTrailersStatus Filter::decodeTrailers(Http::RequestTrailerMap& trailers) {
  return delegateFilterActionOr(delegated_filter_, &StreamDecoderFilter::decodeTrailers,
                                Http::FilterTrailersStatus::Continue, trailers);
}

Http::FilterMetadataStatus Filter::decodeMetadata(Http::MetadataMap& metadata_map) {
  return delegateFilterActionOr(delegated_filter_, &StreamDecoderFilter::decodeMetadata,
                                Http::FilterMetadataStatus::Continue, metadata_map);
}

void Filter::decodeComplete() {
  if (delegated_filter_) {
    delegated_filter_->decodeComplete();
  }
}

Http::Filter1xxHeadersStatus Filter::encode1xxHeaders(Http::ResponseHeaderMap& headers) {
  return delegateFilterActionOr(delegated_filter_, &StreamEncoderFilter::encode1xxHeaders,
                                Http::Filter1xxHeadersStatus::Continue, headers);
}

Http::FilterHeadersStatus Filter::encodeHeaders(Http::ResponseHeaderMap& headers, bool end_stream) {
  return delegateFilterActionOr(delegated_filter_, &StreamEncoderFilter::encodeHeaders,
                                Http::FilterHeadersStatus::Continue, headers, end_stream);
}
Http::FilterDataStatus Filter::encodeData(Buffer::Instance& data, bool end_stream) {
  return delegateFilterActionOr(delegated_filter_, &StreamEncoderFilter::encodeData,
                                Http::FilterDataStatus::Continue, data, end_stream);
}
Http::FilterTrailersStatus Filter::encodeTrailers(Http::ResponseTrailerMap& trailers) {
  return delegateFilterActionOr(delegated_filter_, &StreamEncoderFilter::encodeTrailers,
                                Http::FilterTrailersStatus::Continue, trailers);
}
Http::FilterMetadataStatus Filter::encodeMetadata(Http::MetadataMap& metadata_map) {
  return delegateFilterActionOr(delegated_filter_, &StreamEncoderFilter::encodeMetadata,
                                Http::FilterMetadataStatus::Continue, metadata_map);
}
void Filter::encodeComplete() {
  if (delegated_filter_) {
    delegated_filter_->encodeComplete();
  }
}

void Filter::onMatchCallback(const Matcher::Action& action) {
  const auto& composite_action = action.getTyped<ExecuteFilterAction>();

  // Handle named filter chain lookup.
  if (composite_action.isNamedFilterChainLookup()) {
    // Check sampling first.
    if (composite_action.actionSkip()) {
      return;
    }

    const std::string& chain_name = composite_action.filterChainName();

    // Soft fail: if no named filter chains are configured, do nothing.
    if (!named_filter_chains_) {
      ENVOY_LOG(debug, "filter_chain_name '{}' specified but no named filter chains configured",
                chain_name);
      return;
    }

    // Look up the filter chain by name.
    auto it = named_filter_chains_->find(chain_name);
    if (it == named_filter_chains_->end()) {
      // Soft fail: if the named filter chain is not found, do nothing.
      ENVOY_LOG(debug, "filter_chain_name '{}' not found in named filter chains", chain_name);
      return;
    }

    // Create filters from the pre-compiled factories.
    FactoryCallbacksWrapper wrapper(*this, dispatcher_, true /* is_filter_chain */);
    for (const auto& factory_cb : it->second) {
      factory_cb(wrapper);
    }

    if (!wrapper.filters_to_inject_.empty()) {
      stats_.filter_delegation_success_.inc();
      delegated_filter_ =
          std::make_shared<DelegatedFilterChain>(std::move(wrapper.filters_to_inject_));
      updateFilterState(decoder_callbacks_, std::string(decoder_callbacks_->filterConfigName()),
                        chain_name);
      delegated_filter_->setDecoderFilterCallbacks(*decoder_callbacks_);
      delegated_filter_->setEncoderFilterCallbacks(*encoder_callbacks_);
      access_loggers_.insert(access_loggers_.end(), wrapper.access_loggers_.begin(),
                             wrapper.access_loggers_.end());
    }
    return;
  }

  // Use filter chain mode if the action is a filter chain.
  const bool is_filter_chain = composite_action.isFilterChain();
  FactoryCallbacksWrapper wrapper(*this, dispatcher_, is_filter_chain);
  composite_action.createFilters(wrapper);

  if (!wrapper.errors_.empty()) {
    stats_.filter_delegation_error_.inc();
    ENVOY_LOG(debug, "failed to create delegated filter {}",
              accumulateToString<absl::Status>(
                  wrapper.errors_, [](const auto& status) { return status.ToString(); }));
    return;
  }

  const std::string& action_name = composite_action.actionName();

  // Handle filter chain mode.
  if (is_filter_chain) {
    if (!wrapper.filters_to_inject_.empty()) {
      stats_.filter_delegation_success_.inc();
      delegated_filter_ =
          std::make_shared<DelegatedFilterChain>(std::move(wrapper.filters_to_inject_));
      updateFilterState(decoder_callbacks_, std::string(decoder_callbacks_->filterConfigName()),
                        action_name);
      delegated_filter_->setDecoderFilterCallbacks(*decoder_callbacks_);
      delegated_filter_->setEncoderFilterCallbacks(*encoder_callbacks_);
      access_loggers_.insert(access_loggers_.end(), wrapper.access_loggers_.begin(),
                             wrapper.access_loggers_.end());
    }
    return;
  }

  // Handle single filter mode.
  if (wrapper.filter_to_inject_.has_value()) {
    stats_.filter_delegation_success_.inc();

    auto createDelegatedFilterFn = Overloaded{
        [this, action_name](Http::StreamDecoderFilterSharedPtr filter) {
          delegated_filter_ = std::make_shared<StreamFilterWrapper>(std::move(filter));
          updateFilterState(decoder_callbacks_, std::string(decoder_callbacks_->filterConfigName()),
                            action_name);
        },
        [this, action_name](Http::StreamEncoderFilterSharedPtr filter) {
          delegated_filter_ = std::make_shared<StreamFilterWrapper>(std::move(filter));
          updateFilterState(encoder_callbacks_, std::string(encoder_callbacks_->filterConfigName()),
                            action_name);
        },
        [this, action_name](Http::StreamFilterSharedPtr filter) {
          delegated_filter_ = std::move(filter);
          updateFilterState(decoder_callbacks_, std::string(decoder_callbacks_->filterConfigName()),
                            action_name);
        }};
    absl::visit(createDelegatedFilterFn, std::move(wrapper.filter_to_inject_.value()));

    delegated_filter_->setDecoderFilterCallbacks(*decoder_callbacks_);
    delegated_filter_->setEncoderFilterCallbacks(*encoder_callbacks_);

    // Size should be small, so a copy should be fine.
    access_loggers_.insert(access_loggers_.end(), wrapper.access_loggers_.begin(),
                           wrapper.access_loggers_.end());
  }
  // TODO(snowp): Make it possible for onMatchCallback to fail the stream by issuing a local reply,
  // either directly or via some return status.
}

void Filter::updateFilterState(Http::StreamFilterCallbacks* callback,
                               const std::string& filter_name, const std::string& action_name) {
  if (isUpstream()) {
    return;
  }
  auto* info = callback->streamInfo().filterState()->getDataMutable<MatchedActionInfo>(
      MatchedActionsFilterStateKey);
  if (info != nullptr) {
    info->setFilterAction(filter_name, action_name);
  } else {
    callback->streamInfo().filterState()->setData(
        MatchedActionsFilterStateKey, std::make_shared<MatchedActionInfo>(filter_name, action_name),
        StreamInfo::FilterState::StateType::Mutable,
        StreamInfo::FilterState::LifeSpan::FilterChain);
  }
}

Http::FilterHeadersStatus
Filter::StreamFilterWrapper::decodeHeaders(Http::RequestHeaderMap& headers, bool end_stream) {
  return delegateFilterActionOr(decoder_filter_, &StreamDecoderFilter::decodeHeaders,
                                Http::FilterHeadersStatus::Continue, headers, end_stream);
}
Http::FilterDataStatus Filter::StreamFilterWrapper::decodeData(Buffer::Instance& data,
                                                               bool end_stream) {
  return delegateFilterActionOr(decoder_filter_, &StreamDecoderFilter::decodeData,
                                Http::FilterDataStatus::Continue, data, end_stream);
}
Http::FilterTrailersStatus
Filter::StreamFilterWrapper::decodeTrailers(Http::RequestTrailerMap& trailers) {
  return delegateFilterActionOr(decoder_filter_, &StreamDecoderFilter::decodeTrailers,
                                Http::FilterTrailersStatus::Continue, trailers);
}
Http::FilterMetadataStatus
Filter::StreamFilterWrapper::decodeMetadata(Http::MetadataMap& metadata_map) {
  return delegateFilterActionOr(decoder_filter_, &StreamDecoderFilter::decodeMetadata,
                                Http::FilterMetadataStatus::Continue, metadata_map);
}
void Filter::StreamFilterWrapper::setDecoderFilterCallbacks(
    Http::StreamDecoderFilterCallbacks& callbacks) {
  if (decoder_filter_) {
    decoder_filter_->setDecoderFilterCallbacks(callbacks);
  }
}
void Filter::StreamFilterWrapper::decodeComplete() {
  if (decoder_filter_) {
    decoder_filter_->decodeComplete();
  }
}

Http::Filter1xxHeadersStatus
Filter::StreamFilterWrapper::encode1xxHeaders(Http::ResponseHeaderMap& headers) {
  return delegateFilterActionOr(encoder_filter_, &StreamEncoderFilter::encode1xxHeaders,
                                Http::Filter1xxHeadersStatus::Continue, headers);
}
Http::FilterHeadersStatus
Filter::StreamFilterWrapper::encodeHeaders(Http::ResponseHeaderMap& headers, bool end_stream) {
  return delegateFilterActionOr(encoder_filter_, &StreamEncoderFilter::encodeHeaders,
                                Http::FilterHeadersStatus::Continue, headers, end_stream);
}
Http::FilterDataStatus Filter::StreamFilterWrapper::encodeData(Buffer::Instance& data,
                                                               bool end_stream) {
  return delegateFilterActionOr(encoder_filter_, &StreamEncoderFilter::encodeData,
                                Http::FilterDataStatus::Continue, data, end_stream);
}
Http::FilterTrailersStatus
Filter::StreamFilterWrapper::encodeTrailers(Http::ResponseTrailerMap& trailers) {
  return delegateFilterActionOr(encoder_filter_, &StreamEncoderFilter::encodeTrailers,
                                Http::FilterTrailersStatus::Continue, trailers);
}
Http::FilterMetadataStatus
Filter::StreamFilterWrapper::encodeMetadata(Http::MetadataMap& metadata_map) {
  return delegateFilterActionOr(encoder_filter_, &StreamEncoderFilter::encodeMetadata,
                                Http::FilterMetadataStatus::Continue, metadata_map);
}
void Filter::StreamFilterWrapper::setEncoderFilterCallbacks(
    Http::StreamEncoderFilterCallbacks& callbacks) {
  if (encoder_filter_) {
    encoder_filter_->setEncoderFilterCallbacks(callbacks);
  }
}
void Filter::StreamFilterWrapper::encodeComplete() {
  if (encoder_filter_) {
    encoder_filter_->encodeComplete();
  }
}

// Http::StreamFilterBase
void Filter::StreamFilterWrapper::onDestroy() {
  if (decoder_filter_) {
    decoder_filter_->onDestroy();
  }
  if (encoder_filter_) {
    encoder_filter_->onDestroy();
  }
}

void Filter::StreamFilterWrapper::onStreamComplete() {
  if (decoder_filter_) {
    decoder_filter_->onStreamComplete();
  }

  if (encoder_filter_) {
    encoder_filter_->onStreamComplete();
  }
}

// DelegatedFilterChain implementation.
// For decode operations, iterate filters in order from first to last.
// For encode operations, iterate filters in reverse order from last to first.
Http::FilterHeadersStatus DelegatedFilterChain::decodeHeaders(Http::RequestHeaderMap& headers,
                                                              bool end_stream) {
  for (auto& filter : filters_) {
    auto status = filter->decodeHeaders(headers, end_stream);
    if (status != Http::FilterHeadersStatus::Continue) {
      return status;
    }
  }
  return Http::FilterHeadersStatus::Continue;
}

Http::FilterDataStatus DelegatedFilterChain::decodeData(Buffer::Instance& data, bool end_stream) {
  for (auto& filter : filters_) {
    auto status = filter->decodeData(data, end_stream);
    if (status != Http::FilterDataStatus::Continue) {
      return status;
    }
  }
  return Http::FilterDataStatus::Continue;
}

Http::FilterTrailersStatus DelegatedFilterChain::decodeTrailers(Http::RequestTrailerMap& trailers) {
  for (auto& filter : filters_) {
    auto status = filter->decodeTrailers(trailers);
    if (status != Http::FilterTrailersStatus::Continue) {
      return status;
    }
  }
  return Http::FilterTrailersStatus::Continue;
}

Http::FilterMetadataStatus DelegatedFilterChain::decodeMetadata(Http::MetadataMap& metadata_map) {
  for (auto& filter : filters_) {
    auto status = filter->decodeMetadata(metadata_map);
    if (status != Http::FilterMetadataStatus::Continue) {
      return status;
    }
  }
  return Http::FilterMetadataStatus::Continue;
}

void DelegatedFilterChain::setDecoderFilterCallbacks(
    Http::StreamDecoderFilterCallbacks& callbacks) {
  for (auto& filter : filters_) {
    filter->setDecoderFilterCallbacks(callbacks);
  }
}

void DelegatedFilterChain::decodeComplete() {
  for (auto& filter : filters_) {
    filter->decodeComplete();
  }
}

Http::Filter1xxHeadersStatus
DelegatedFilterChain::encode1xxHeaders(Http::ResponseHeaderMap& headers) {
  // Encode operations iterate in reverse order.
  for (auto it = filters_.rbegin(); it != filters_.rend(); ++it) {
    auto status = (*it)->encode1xxHeaders(headers);
    if (status != Http::Filter1xxHeadersStatus::Continue) {
      return status;
    }
  }
  return Http::Filter1xxHeadersStatus::Continue;
}

Http::FilterHeadersStatus DelegatedFilterChain::encodeHeaders(Http::ResponseHeaderMap& headers,
                                                              bool end_stream) {
  // Encode operations iterate in reverse order.
  for (auto it = filters_.rbegin(); it != filters_.rend(); ++it) {
    auto status = (*it)->encodeHeaders(headers, end_stream);
    if (status != Http::FilterHeadersStatus::Continue) {
      return status;
    }
  }
  return Http::FilterHeadersStatus::Continue;
}

Http::FilterDataStatus DelegatedFilterChain::encodeData(Buffer::Instance& data, bool end_stream) {
  // Encode operations iterate in reverse order.
  for (auto it = filters_.rbegin(); it != filters_.rend(); ++it) {
    auto status = (*it)->encodeData(data, end_stream);
    if (status != Http::FilterDataStatus::Continue) {
      return status;
    }
  }
  return Http::FilterDataStatus::Continue;
}

Http::FilterTrailersStatus
DelegatedFilterChain::encodeTrailers(Http::ResponseTrailerMap& trailers) {
  // Encode operations iterate in reverse order.
  for (auto it = filters_.rbegin(); it != filters_.rend(); ++it) {
    auto status = (*it)->encodeTrailers(trailers);
    if (status != Http::FilterTrailersStatus::Continue) {
      return status;
    }
  }
  return Http::FilterTrailersStatus::Continue;
}

Http::FilterMetadataStatus DelegatedFilterChain::encodeMetadata(Http::MetadataMap& metadata_map) {
  // Encode operations iterate in reverse order.
  for (auto it = filters_.rbegin(); it != filters_.rend(); ++it) {
    auto status = (*it)->encodeMetadata(metadata_map);
    if (status != Http::FilterMetadataStatus::Continue) {
      return status;
    }
  }
  return Http::FilterMetadataStatus::Continue;
}

void DelegatedFilterChain::setEncoderFilterCallbacks(
    Http::StreamEncoderFilterCallbacks& callbacks) {
  for (auto& filter : filters_) {
    filter->setEncoderFilterCallbacks(callbacks);
  }
}

void DelegatedFilterChain::encodeComplete() {
  // Encode operations iterate in reverse order.
  for (auto it = filters_.rbegin(); it != filters_.rend(); ++it) {
    (*it)->encodeComplete();
  }
}

void DelegatedFilterChain::onDestroy() {
  for (auto& filter : filters_) {
    static_cast<Http::StreamDecoderFilter&>(*filter).onDestroy();
  }
}

void DelegatedFilterChain::onStreamComplete() {
  for (auto& filter : filters_) {
    static_cast<Http::StreamDecoderFilter&>(*filter).onStreamComplete();
  }
}

} // namespace Composite
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
