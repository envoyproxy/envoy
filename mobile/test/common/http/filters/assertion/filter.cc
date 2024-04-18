#include "test/common/http/filters/assertion/filter.h"

#include "envoy/http/codes.h"
#include "envoy/server/filter_config.h"

#include "source/common/http/header_map_impl.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Assertion {

AssertionFilterConfig::AssertionFilterConfig(
    const envoymobile::extensions::filters::http::assertion::Assertion& proto_config,
    Server::Configuration::CommonFactoryContext& context) {
  Common::Matcher::buildMatcher(proto_config.match_config(), matchers_, context);
}

Extensions::Common::Matcher::Matcher& AssertionFilterConfig::rootMatcher() const {
  ASSERT(!matchers_.empty());
  return *matchers_[0];
}

// Implementation of this filter is complicated by the fact that the streaming matchers have no
// explicit mechanism to handle end_stream. This means that we must infer that matching has failed
// if the stream ends with still-unsatisfied matches. We do this by potentially passing empty
// body data and empty trailers to the matchers in the event the stream ends without including
// these entities.
AssertionFilter::AssertionFilter(AssertionFilterConfigSharedPtr config) : config_(config) {
  statuses_ = Extensions::Common::Matcher::Matcher::MatchStatusVector(config_->matchersSize());
  config_->rootMatcher().onNewStream(statuses_);
}

Http::FilterHeadersStatus AssertionFilter::decodeHeaders(Http::RequestHeaderMap& headers,
                                                         bool end_stream) {
  config_->rootMatcher().onHttpRequestHeaders(headers, statuses_);
  auto& match_status = config_->rootMatcher().matchStatus(statuses_);
  if (!match_status.matches_ && !match_status.might_change_status_) {
    decoder_callbacks_->sendLocalReply(Http::Code::BadRequest,
                                       "Request Headers do not match configured expectations",
                                       nullptr, absl::nullopt, "");
    return Http::FilterHeadersStatus::StopIteration;
  }

  if (end_stream) {
    // Check if there are unsatisfied assertions about stream trailers.
    auto empty_trailers = Http::RequestTrailerMapImpl::create();
    config_->rootMatcher().onHttpRequestTrailers(*empty_trailers, statuses_);
    auto& final_match_status = config_->rootMatcher().matchStatus(statuses_);
    if (!final_match_status.matches_ && !final_match_status.might_change_status_) {
      decoder_callbacks_->sendLocalReply(Http::Code::BadRequest,
                                         "Request Trailers do not match configured expectations",
                                         nullptr, absl::nullopt, "");
      return Http::FilterHeadersStatus::StopIteration;
    }

    // Because a stream only contains a single set of headers or trailers, if either fail to
    // satisfy assertions, might_change_status_ will be false. Therefore if matches_ is still
    // unsatisfied here, it must be because of body data.
    if (!final_match_status.matches_) {
      decoder_callbacks_->sendLocalReply(Http::Code::BadRequest,
                                         "Request Body does not match configured expectations",
                                         nullptr, absl::nullopt, "");
      return Http::FilterHeadersStatus::StopIteration;
    }
  }

  return Http::FilterHeadersStatus::Continue;
}

Http::FilterDataStatus AssertionFilter::decodeData(Buffer::Instance& data, bool end_stream) {
  config_->rootMatcher().onRequestBody(data, statuses_);
  auto& match_status = config_->rootMatcher().matchStatus(statuses_);
  if (!match_status.matches_ && !match_status.might_change_status_) {
    decoder_callbacks_->sendLocalReply(Http::Code::BadRequest,
                                       "Request Body does not match configured expectations",
                                       nullptr, absl::nullopt, "");
    return Http::FilterDataStatus::StopIterationNoBuffer;
  }

  if (end_stream) {
    // Check if there are unsatisfied assertions about stream trailers.
    auto empty_trailers = Http::RequestTrailerMapImpl::create();
    config_->rootMatcher().onHttpRequestTrailers(*empty_trailers, statuses_);
    auto& match_status = config_->rootMatcher().matchStatus(statuses_);
    if (!match_status.matches_ && !match_status.might_change_status_) {
      decoder_callbacks_->sendLocalReply(Http::Code::BadRequest,
                                         "Request Trailers do not match configured expectations",
                                         nullptr, absl::nullopt, "");
      return Http::FilterDataStatus::StopIterationNoBuffer;
    }

    // Because a stream only contains a single set of headers or trailers, if either fail to
    // satisfy assertions, might_change_status_ will be false. Therefore if matches_ is still
    // unsatisfied here, it must be because of body data.
    if (!match_status.matches_) {
      decoder_callbacks_->sendLocalReply(Http::Code::BadRequest,
                                         "Request Body does not match configured expectations",
                                         nullptr, absl::nullopt, "");
      return Http::FilterDataStatus::StopIterationNoBuffer;
    }
  }
  return Http::FilterDataStatus::Continue;
}

Http::FilterTrailersStatus AssertionFilter::decodeTrailers(Http::RequestTrailerMap& trailers) {
  config_->rootMatcher().onHttpRequestTrailers(trailers, statuses_);
  auto& match_status = config_->rootMatcher().matchStatus(statuses_);
  if (!match_status.matches_ && !match_status.might_change_status_) {
    decoder_callbacks_->sendLocalReply(Http::Code::BadRequest,
                                       "Request Trailers do not match configured expectations",
                                       nullptr, absl::nullopt, "");
    return Http::FilterTrailersStatus::StopIteration;
  }

  // Because a stream only contains a single set of headers or trailers, if either fail to
  // satisfy assertions, might_change_status_ will be false. Therefore if matches_ is still
  // unsatisfied here, it must be because of body data.
  if (!match_status.matches_) {
    decoder_callbacks_->sendLocalReply(Http::Code::BadRequest,
                                       "Request Body does not match configured expectations",
                                       nullptr, absl::nullopt, "");
    return Http::FilterTrailersStatus::StopIteration;
  }
  return Http::FilterTrailersStatus::Continue;
}

Http::FilterHeadersStatus AssertionFilter::encodeHeaders(Http::ResponseHeaderMap& headers,
                                                         bool end_stream) {
  config_->rootMatcher().onHttpResponseHeaders(headers, statuses_);
  auto& match_status = config_->rootMatcher().matchStatus(statuses_);
  if (!match_status.matches_ && !match_status.might_change_status_) {
    decoder_callbacks_->sendLocalReply(Http::Code::InternalServerError,
                                       "Response Headers do not match configured expectations",
                                       nullptr, absl::nullopt, "");
    return Http::FilterHeadersStatus::StopIteration;
  }

  if (end_stream) {
    // Check if there are unsatisfied assertions about stream trailers.
    auto empty_trailers = Http::ResponseTrailerMapImpl::create();
    config_->rootMatcher().onHttpResponseTrailers(*empty_trailers, statuses_);
    auto& final_match_status = config_->rootMatcher().matchStatus(statuses_);
    if (!final_match_status.matches_ && !final_match_status.might_change_status_) {
      decoder_callbacks_->sendLocalReply(Http::Code::InternalServerError,
                                         "Response Trailers do not match configured expectations",
                                         nullptr, absl::nullopt, "");
      return Http::FilterHeadersStatus::StopIteration;
    }

    // Because a stream only contains a single set of headers or trailers, if either fail to
    // satisfy assertions, might_change_status_ will be false. Therefore if matches_ is still
    // unsatisfied here, it must be because of body data.
    if (!final_match_status.matches_) {
      decoder_callbacks_->sendLocalReply(Http::Code::InternalServerError,
                                         "Response Body does not match configured expectations",
                                         nullptr, absl::nullopt, "");
      return Http::FilterHeadersStatus::StopIteration;
    }
  }

  return Http::FilterHeadersStatus::Continue;
}

Http::FilterDataStatus AssertionFilter::encodeData(Buffer::Instance& data, bool end_stream) {
  config_->rootMatcher().onResponseBody(data, statuses_);
  auto& match_status = config_->rootMatcher().matchStatus(statuses_);
  if (!match_status.matches_ && !match_status.might_change_status_) {
    decoder_callbacks_->sendLocalReply(Http::Code::InternalServerError,
                                       "Response Body does not match configured expectations",
                                       nullptr, absl::nullopt, "");
    return Http::FilterDataStatus::StopIterationNoBuffer;
  }

  if (end_stream) {
    // Check if there are unsatisfied assertions about stream trailers.
    auto empty_trailers = Http::ResponseTrailerMapImpl::create();
    config_->rootMatcher().onHttpResponseTrailers(*empty_trailers, statuses_);
    auto& match_status = config_->rootMatcher().matchStatus(statuses_);
    if (!match_status.matches_ && !match_status.might_change_status_) {
      decoder_callbacks_->sendLocalReply(Http::Code::InternalServerError,
                                         "Response Trailers do not match configured expectations",
                                         nullptr, absl::nullopt, "");
      return Http::FilterDataStatus::StopIterationNoBuffer;
    }

    // Because a stream only contains a single set of headers or trailers, if either fail to
    // satisfy assertions, might_change_status_ will be false. Therefore if matches_ is still
    // unsatisfied here, it must be because of body data.
    if (!match_status.matches_) {
      decoder_callbacks_->sendLocalReply(Http::Code::InternalServerError,
                                         "Response Body does not match configured expectations",
                                         nullptr, absl::nullopt, "");
      return Http::FilterDataStatus::StopIterationNoBuffer;
    }
  }
  return Http::FilterDataStatus::Continue;
}

Http::FilterTrailersStatus AssertionFilter::encodeTrailers(Http::ResponseTrailerMap& trailers) {
  config_->rootMatcher().onHttpResponseTrailers(trailers, statuses_);
  auto& match_status = config_->rootMatcher().matchStatus(statuses_);
  if (!match_status.matches_ && !match_status.might_change_status_) {
    decoder_callbacks_->sendLocalReply(Http::Code::InternalServerError,
                                       "Response Trailers do not match configured expectations",
                                       nullptr, absl::nullopt, "");
    return Http::FilterTrailersStatus::StopIteration;
  }

  // Because a stream only contains a single set of headers or trailers, if either fail to
  // satisfy assertions, might_change_status_ will be false. Therefore if matches_ is still
  // unsatisfied here, it must be because of body data.
  if (!match_status.matches_) {
    decoder_callbacks_->sendLocalReply(Http::Code::InternalServerError,
                                       "Response Body does not match configured expectations",
                                       nullptr, absl::nullopt, "");
    return Http::FilterTrailersStatus::StopIteration;
  }
  return Http::FilterTrailersStatus::Continue;
}

} // namespace Assertion
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
