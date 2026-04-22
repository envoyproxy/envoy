#include <string>

#include "envoy/http/filter.h"
#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

#include "source/extensions/filters/http/common/pass_through_filter.h"

#include "test/extensions/filters/http/common/empty_http_filter_config.h"
#include "test/integration/filters/common.h"

namespace Envoy {

class LocalReplyDuringDecode : public Http::PassThroughFilter, public Http::UpstreamCallbacks {
public:
  constexpr static char name[] = "local-reply-during-decode";

  void setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) override {
    decoder_callbacks_ = &callbacks;
    if (auto cb = decoder_callbacks_->upstreamCallbacks(); cb) {
      cb->addUpstreamCallbacks(*this);
    }
  }

  void onUpstreamConnectionEstablished() override {
    if (latched_end_stream_.has_value()) {
      const bool end_stream = *latched_end_stream_;
      latched_end_stream_.reset();
      Http::FilterHeadersStatus status = decodeHeaders(*request_headers_, end_stream);
      if (status == Http::FilterHeadersStatus::Continue) {
        decoder_callbacks_->continueDecoding();
      }
    }
  }

  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap& request_headers,
                                          bool end_stream) override {
    // If this filter is being used as an upstream filter and the upstream connection is not yet
    // established, check for the "wait-upstream-connection" header to determine whether to wait
    // for the connection to be established before continuing decoding.
    if (auto cb = decoder_callbacks_->upstreamCallbacks(); cb && !cb->upstream()) {
      auto result = request_headers.get(Http::LowerCaseString("wait-upstream-connection"));
      if (!result.empty() && result[0]->value() == "true") {
        request_headers_ = &request_headers;
        latched_end_stream_ = end_stream;
        return Http::FilterHeadersStatus::StopAllIterationAndBuffer;
      }
    }

    auto result = request_headers.get(Http::LowerCaseString("skip-local-reply"));
    if (!result.empty() && result[0]->value() == "true") {
      header_local_reply_skipped_ = true;
      return Http::FilterHeadersStatus::Continue;
    }
    result = request_headers.get(Http::LowerCaseString("local-reply-during-data"));
    if (!result.empty() && result[0]->value() == "true") {
      local_reply_during_data_ = true;
      header_local_reply_skipped_ = true;
      return Http::FilterHeadersStatus::Continue;
    }
    decoder_callbacks_->sendLocalReply(Http::Code::InternalServerError, "", nullptr, absl::nullopt,
                                       "");
    return Http::FilterHeadersStatus::StopIteration;
  }

  Http::FilterDataStatus decodeData(Buffer::Instance&, bool) override {
    ASSERT(header_local_reply_skipped_);
    if (local_reply_during_data_) {
      decoder_callbacks_->sendLocalReply(Http::Code::InternalServerError, "", nullptr,
                                         absl::nullopt, "");
    }
    return Http::FilterDataStatus::Continue;
  }

  Http::FilterMetadataStatus decodeMetadata(Http::MetadataMap&) override {
    ASSERT(header_local_reply_skipped_);
    return Http::FilterMetadataStatus::Continue;
  }

private:
  Http::RequestHeaderMap* request_headers_{};
  absl::optional<bool> latched_end_stream_;
  bool header_local_reply_skipped_ = false;
  bool local_reply_during_data_ = false;
};

constexpr char LocalReplyDuringDecode::name[];
static Registry::RegisterFactory<SimpleFilterConfig<LocalReplyDuringDecode>,
                                 Server::Configuration::NamedHttpFilterConfigFactory>
    register_;
static Registry::RegisterFactory<SimpleFilterConfig<LocalReplyDuringDecode>,
                                 Server::Configuration::UpstreamHttpFilterConfigFactory>
    register_upstream_;

} // namespace Envoy
