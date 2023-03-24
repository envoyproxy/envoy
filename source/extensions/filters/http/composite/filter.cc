#include "source/extensions/filters/http/composite/filter.h"

#include "envoy/http/filter.h"

#include "source/common/common/stl_helpers.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Composite {

namespace {
// Helper that returns the last `filter->func(args...)` result in the list of filters, if the list
// is empty, return `rval`.
template <class FilterPtrT, class FuncT, class RValT, class... Args>
RValT delegateAllFilterActionOr(std::vector<FilterPtrT>& filter_list, FuncT func, RValT rval,
                                Args&&... args) {
  RValT value = rval;
  for (auto filter = filter_list.rbegin(); filter != filter_list.rend(); ++filter) {
    value = ((**filter).*func)(std::forward<Args>(args)...);
  }
  return value;
}
// Helper that returns `filter->func(args...)` if the filter is not null, returning `rval`
// otherwise.
template <class FilterPtrT, class FuncT, class RValT, class... Args>
RValT delegateFilterActionOr(FilterPtrT& filter, FuncT func, RValT rval, Args&&... args) {
  if (filter) {
    return ((*filter).*func)(std::forward<Args>(args)...);
  }
  return rval;
}
} // namespace
Http::FilterHeadersStatus Filter::decodeHeaders(Http::RequestHeaderMap& headers, bool end_stream) {
  decoded_headers_ = true;

  return delegateAllFilterActionOr(delegated_filter_list_, &StreamDecoderFilter::decodeHeaders,
                                   Http::FilterHeadersStatus::Continue, headers, end_stream);
}

Http::FilterDataStatus Filter::decodeData(Buffer::Instance& data, bool end_stream) {
  return delegateAllFilterActionOr(delegated_filter_list_, &StreamDecoderFilter::decodeData,
                                   Http::FilterDataStatus::Continue, data, end_stream);
}

Http::FilterTrailersStatus Filter::decodeTrailers(Http::RequestTrailerMap& trailers) {
  return delegateAllFilterActionOr(delegated_filter_list_, &StreamDecoderFilter::decodeTrailers,
                                   Http::FilterTrailersStatus::Continue, trailers);
}

Http::FilterMetadataStatus Filter::decodeMetadata(Http::MetadataMap& metadata_map) {
  return delegateAllFilterActionOr(delegated_filter_list_, &StreamDecoderFilter::decodeMetadata,
                                   Http::FilterMetadataStatus::Continue, metadata_map);
}

void Filter::decodeComplete() {
  for (auto delegated_filter : delegated_filter_list_) {
    delegated_filter->decodeComplete();
  }
}

Http::Filter1xxHeadersStatus Filter::encode1xxHeaders(Http::ResponseHeaderMap& headers) {
  return delegateAllFilterActionOr(delegated_filter_list_, &StreamEncoderFilter::encode1xxHeaders,
                                   Http::Filter1xxHeadersStatus::Continue, headers);
}

Http::FilterHeadersStatus Filter::encodeHeaders(Http::ResponseHeaderMap& headers, bool end_stream) {
  return delegateAllFilterActionOr(delegated_filter_list_, &StreamEncoderFilter::encodeHeaders,
                                   Http::FilterHeadersStatus::Continue, headers, end_stream);
}
Http::FilterDataStatus Filter::encodeData(Buffer::Instance& data, bool end_stream) {
  return delegateAllFilterActionOr(delegated_filter_list_, &StreamEncoderFilter::encodeData,
                                   Http::FilterDataStatus::Continue, data, end_stream);
}
Http::FilterTrailersStatus Filter::encodeTrailers(Http::ResponseTrailerMap& trailers) {
  return delegateAllFilterActionOr(delegated_filter_list_, &StreamEncoderFilter::encodeTrailers,
                                   Http::FilterTrailersStatus::Continue, trailers);
}
Http::FilterMetadataStatus Filter::encodeMetadata(Http::MetadataMap& metadata_map) {
  return delegateAllFilterActionOr(delegated_filter_list_, &StreamEncoderFilter::encodeMetadata,
                                   Http::FilterMetadataStatus::Continue, metadata_map);
}
void Filter::encodeComplete() {
  for (auto delegated_filter : delegated_filter_list_) {
    delegated_filter->encodeComplete();
  }
}

void Filter::ApplyWrapper(FactoryCallbacksWrapper& wrapper) {
  if (!wrapper.errors_.empty()) {
    stats_.filter_delegation_error_.inc();
    ENVOY_LOG(debug, "failed to create delegated filter {}",
              accumulateToString<absl::Status>(
                  wrapper.errors_, [](const auto& status) { return status.ToString(); }));
    return;
  }

  if (wrapper.filter_to_inject_) {
    stats_.filter_delegation_success_.inc();
    Http::StreamFilterSharedPtr delegated_filter;
    if (absl::holds_alternative<Http::StreamDecoderFilterSharedPtr>(*wrapper.filter_to_inject_)) {
      delegated_filter = std::make_shared<StreamFilterWrapper>(
          absl::get<Http::StreamDecoderFilterSharedPtr>(*wrapper.filter_to_inject_));
    } else if (absl::holds_alternative<Http::StreamEncoderFilterSharedPtr>(
                   *wrapper.filter_to_inject_)) {
      delegated_filter = std::make_shared<StreamFilterWrapper>(
          absl::get<Http::StreamEncoderFilterSharedPtr>(*wrapper.filter_to_inject_));
    } else {
      delegated_filter = absl::get<Http::StreamFilterSharedPtr>(*wrapper.filter_to_inject_);
    }

    delegated_filter->setDecoderFilterCallbacks(*decoder_callbacks_);
    delegated_filter->setEncoderFilterCallbacks(*encoder_callbacks_);
    delegated_filter_list_.push_back(delegated_filter);

    // Size should be small, so a copy should be fine.
    access_loggers_.insert(access_loggers_.end(), wrapper.access_loggers_.begin(),
                           wrapper.access_loggers_.end());
  }

  // TODO(snowp): Make it possible for onMatchCallback to fail the stream by issuing a local reply,
  // either directly or via some return status.
}

void Filter::onMatchCallback(const Matcher::Action& action) {
  if (action.typeUrl() == ExecuteFilterAction::staticTypeUrl()) {
    const auto& composite_action = action.getTyped<ExecuteFilterAction>();
    FactoryCallbacksWrapper wrapper(*this, dispatcher_);
    composite_action.createFilters(wrapper);
    ApplyWrapper(wrapper);
  } else if (action.typeUrl() == ExecuteFilterMultiAction::staticTypeUrl()) {
    const auto& composite_multi_action = action.getTyped<ExecuteFilterMultiAction>();
    std::function<void(Http::FilterFactoryCb&)> parse_wrapper = [this](Http::FilterFactoryCb& cb) {
      FactoryCallbacksWrapper wrapper(*this, dispatcher_);
      cb(wrapper);
      ApplyWrapper(wrapper);
    };
    composite_multi_action.createFilters(parse_wrapper);
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

} // namespace Composite
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
