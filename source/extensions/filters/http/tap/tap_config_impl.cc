#include "extensions/filters/http/tap/tap_config_impl.h"

#include "envoy/data/tap/v2alpha/http.pb.h"

#include "common/common/assert.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace TapFilter {

HttpTapConfigImpl::HttpTapConfigImpl(envoy::service::tap::v2alpha::TapConfig&& proto_config,
                                     Common::Tap::Sink* admin_streamer)
    : Extensions::Common::Tap::TapConfigBaseImpl(std::move(proto_config)),
      admin_streamer_(admin_streamer) {
  // TODO(mattklein123): The streaming admin output sink is the only currently supported sink.
  ASSERT(admin_streamer);
  ASSERT(proto_config_.output_config().sinks()[0].has_streaming_admin());

  for (const auto& proto_match_config : proto_config_.match_configs()) {
    match_configs_.emplace_back();
    MatchConfig& match_config = match_configs_.back();
    match_config.id_ = proto_match_config.match_id();

    if (proto_match_config.http_match_config().has_request_match_config()) {
      match_config.request_config_ = RequestMatchConfig();
      for (const auto& header_match :
           proto_match_config.http_match_config().request_match_config().headers()) {
        match_config.request_config_.value().headers_to_match_.emplace_back(header_match);
      }
    }

    if (proto_match_config.http_match_config().has_response_match_config()) {
      match_config.response_config_ = ResponseMatchConfig();
      for (const auto& header_match :
           proto_match_config.http_match_config().response_match_config().headers()) {
        match_config.response_config_.value().headers_to_match_.emplace_back(header_match);
      }
    }
  }
}

void HttpTapConfigImpl::matchesRequestHeaders(const Http::HeaderMap& headers,
                                              std::vector<MatchStatus>& statuses) {
  ASSERT(match_configs_.size() == statuses.size());
  for (uint64_t i = 0; i < match_configs_.size(); i++) {
    statuses[i].request_matched_ = true;
    if (match_configs_[i].request_config_.has_value()) {
      statuses[i].request_matched_ = Http::HeaderUtility::matchHeaders(
          headers, match_configs_[i].request_config_.value().headers_to_match_);
    }
  }
}

void HttpTapConfigImpl::matchesResponseHeaders(const Http::HeaderMap& headers,
                                               std::vector<MatchStatus>& statuses) {
  ASSERT(match_configs_.size() == statuses.size());
  for (uint64_t i = 0; i < match_configs_.size(); i++) {
    statuses[i].response_matched_ = true;
    if (match_configs_[i].response_config_.has_value()) {
      statuses[i].response_matched_ = Http::HeaderUtility::matchHeaders(
          headers, match_configs_[i].response_config_.value().headers_to_match_);
    }
  }
}

HttpPerRequestTapperPtr HttpTapConfigImpl::createPerRequestTapper() {
  return std::make_unique<HttpPerRequestTapperImpl>(shared_from_this());
}

void HttpPerRequestTapperImpl::onRequestHeaders(const Http::HeaderMap& headers) {
  config_->matchesRequestHeaders(headers, statuses_);
}

void HttpPerRequestTapperImpl::onResponseHeaders(const Http::HeaderMap& headers) {
  config_->matchesResponseHeaders(headers, statuses_);
}

namespace {
Http::HeaderMap::Iterate fillHeaderList(const Http::HeaderEntry& header, void* context) {
  Protobuf::RepeatedPtrField<envoy::api::v2::core::HeaderValue>& header_list =
      *reinterpret_cast<Protobuf::RepeatedPtrField<envoy::api::v2::core::HeaderValue>*>(context);
  auto& new_header = *header_list.Add();
  new_header.set_key(header.key().c_str());
  new_header.set_value(header.value().c_str());
  return Http::HeaderMap::Iterate::Continue;
}
} // namespace

bool HttpPerRequestTapperImpl::onDestroyLog(const Http::HeaderMap* request_headers,
                                            const Http::HeaderMap* response_headers) {
  absl::optional<uint64_t> match_index;
  for (uint64_t i = 0; i < statuses_.size(); i++) {
    const auto& status = statuses_[i];
    if (status.request_matched_ && status.response_matched_) {
      match_index = i;
      break;
    }
  }

  if (!match_index.has_value()) {
    return false;
  }

  auto trace = std::make_shared<envoy::data::tap::v2alpha::HttpBufferedTrace>();
  trace->set_match_id(config_->configs()[match_index.value()].id_);
  request_headers->iterate(fillHeaderList, trace->mutable_request_headers());
  if (response_headers != nullptr) {
    response_headers->iterate(fillHeaderList, trace->mutable_response_headers());
  }

  ENVOY_LOG(debug, "submitting buffered trace sink");
  config_->sink().submitBufferedTrace(trace);
  return true;
}

} // namespace TapFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
