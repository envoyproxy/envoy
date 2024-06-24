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
template <class... Ts> struct Overloaded : Ts... { using Ts::operator()...; };

template <class... Ts> Overloaded(Ts...) -> Overloaded<Ts...>;

} // namespace

std::unique_ptr<ProtobufWkt::Struct> MatchedActionInfo::buildProtoStruct() const {
  auto message = std::make_unique<ProtobufWkt::Struct>();
  auto& fields = *message->mutable_fields();
  for (const auto& p : actions_) {
    fields[p.first] = ValueUtil::stringValue(p.second);
  }
  return message;
}

Http::FilterHeadersStatus Filter::decodeHeaders(Http::RequestHeaderMap& headers, bool end_stream) {
  decoded_headers_ = true;

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

  FactoryCallbacksWrapper wrapper(*this, dispatcher_);
  composite_action.createFilters(wrapper);

  if (!wrapper.errors_.empty()) {
    stats_.filter_delegation_error_.inc();
    ENVOY_LOG(debug, "failed to create delegated filter {}",
              accumulateToString<absl::Status>(
                  wrapper.errors_, [](const auto& status) { return status.ToString(); }));
    return;
  }
  const std::string& action_name = composite_action.actionName();

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

} // namespace Composite
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
