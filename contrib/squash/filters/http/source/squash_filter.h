#pragma once

#include <regex>

#include "envoy/http/async_client.h"
#include "envoy/http/filter.h"
#include "envoy/json/json_object.h"
#include "envoy/upstream/cluster_manager.h"

#include "source/common/common/logger.h"
#include "source/common/protobuf/protobuf.h"

#include "absl/types/optional.h"
#include "contrib/envoy/extensions/filters/http/squash/v3/squash.pb.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Squash {

class SquashFilterConfig : protected Logger::Loggable<Logger::Id::config> {
public:
  SquashFilterConfig(const envoy::extensions::filters::http::squash::v3::Squash& proto_config,
                     Upstream::ClusterManager& cluster_manager);
  const std::string& clusterName() const { return cluster_name_; }
  const std::string& attachmentJson() const { return attachment_json_; }
  const std::chrono::milliseconds& attachmentTimeout() const { return attachment_timeout_; }
  const std::chrono::milliseconds& attachmentPollPeriod() const { return attachment_poll_period_; }
  const std::chrono::milliseconds& requestTimeout() const { return request_timeout_; }

private:
  // Get the attachment body, and returns a JSON representations with environment variables
  // interpolated.
  static std::string getAttachment(const ProtobufWkt::Struct& attachment_template);
  // Recursively interpolates environment variables inline in the struct.
  static void updateTemplateInStruct(ProtobufWkt::Struct& attachment_template);
  // Recursively interpolates environment variables inline in the value.
  static void updateTemplateInValue(ProtobufWkt::Value& curvalue);
  // Interpolates environment variables in a string, and returns the new interpolated string.
  static std::string replaceEnv(const std::string& attachment_template);

  // The name of the squash server cluster.
  const std::string cluster_name_;
  // The attachment body sent to squash server on create attachment.
  const std::string attachment_json_;
  // The total amount of time for an attachment to reach a final state (attached or error).
  const std::chrono::milliseconds attachment_timeout_;
  // How frequently should we poll the attachment state with the squash server.
  const std::chrono::milliseconds attachment_poll_period_;
  // The timeout for individual requests to the squash server.
  const std::chrono::milliseconds request_timeout_;

  // Defines the pattern for interpolating environment variables in to the attachment.
  const static std::regex ENV_REGEX;
};

using SquashFilterConfigSharedPtr = std::shared_ptr<SquashFilterConfig>;

class AsyncClientCallbackShim : public Http::AsyncClient::Callbacks {
public:
  AsyncClientCallbackShim(std::function<void(Http::ResponseMessagePtr&&)>&& on_success,
                          std::function<void(Http::AsyncClient::FailureReason)>&& on_fail)
      : on_success_(on_success), on_fail_(on_fail) {}
  // Http::AsyncClient::Callbacks
  void onSuccess(const Http::AsyncClient::Request&, Http::ResponseMessagePtr&& m) override {
    on_success_(std::forward<Http::ResponseMessagePtr>(m));
  }
  void onFailure(const Http::AsyncClient::Request&, Http::AsyncClient::FailureReason f) override {
    on_fail_(f);
  }
  void onBeforeFinalizeUpstreamSpan(Tracing::Span&, const Http::ResponseHeaderMap*) override {}

private:
  const std::function<void(Http::ResponseMessagePtr&&)> on_success_;
  const std::function<void(Http::AsyncClient::FailureReason)> on_fail_;
};

class SquashFilter : public Http::StreamDecoderFilter,
                     protected Logger::Loggable<Logger::Id::filter> {
public:
  SquashFilter(SquashFilterConfigSharedPtr config, Upstream::ClusterManager& cm);
  ~SquashFilter() override;

  // Http::StreamFilterBase
  void onDestroy() override;

  // Http::StreamDecoderFilter
  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap& headers, bool) override;
  Http::FilterDataStatus decodeData(Buffer::Instance&, bool) override;
  Http::FilterTrailersStatus decodeTrailers(Http::RequestTrailerMap&) override;
  void setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) override;

private:
  // AsyncClient callbacks for create attachment request
  void onCreateAttachmentSuccess(Http::ResponseMessagePtr&&);
  void onCreateAttachmentFailure(Http::AsyncClient::FailureReason);
  // AsyncClient callbacks for get attachment request
  void onGetAttachmentSuccess(Http::ResponseMessagePtr&&);
  void onGetAttachmentFailure(Http::AsyncClient::FailureReason);

  // Schedules a pollForAttachment
  void scheduleRetry();
  // Contacts Squash server to get the latest version of a debug attachment.
  void pollForAttachment();
  // Cleanup and continue the filter chain.
  void doneSquashing();
  void cleanup();
  // Creates a JSON from the message body.
  Json::ObjectSharedPtr getJsonBody(Http::ResponseMessagePtr&& m);

  const SquashFilterConfigSharedPtr config_;

  // Current state of the squash filter. If is_squashing_ is true, Hold the request while we
  // communicate with the squash server to attach a debugger. If it is false, let the request
  // pass-through.
  bool is_squashing_{false};
  // The API path of the created debug attachment (used for polling its state).
  std::string debug_attachment_path_;
  // A timer for polling the state of a debug attachment until it reaches a final state.
  Event::TimerPtr attachment_poll_period_timer_;
  // A timeout timer - after this timer expires we abort polling the debug attachment, and continue
  // filter iteration
  Event::TimerPtr attachment_timeout_timer_;
  // The current inflight request to the squash server.
  Http::AsyncClient::Request* in_flight_request_{nullptr};
  // Shims to get AsyncClient callbacks to specific methods, per API method.
  AsyncClientCallbackShim create_attachment_callback_;
  AsyncClientCallbackShim check_attachment_callback_;

  // ClusterManager to send requests to squash server
  Upstream::ClusterManager& cm_;
  // Callbacks used to continue filter iteration.
  Http::StreamDecoderFilterCallbacks* decoder_callbacks_{nullptr};

  // Create debug attachment URL path.
  const static std::string POST_ATTACHMENT_PATH;
  // Authority header for squash server.
  const static std::string SERVER_AUTHORITY;
  // The state of a debug attachment object when a debugger is successfully attached.
  const static std::string ATTACHED_STATE;
  // The state of a debug attachment object when an error has occurred.
  const static std::string ERROR_STATE;
};

} // namespace Squash
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
