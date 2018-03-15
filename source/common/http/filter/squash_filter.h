#pragma once

#include <map>
#include <regex>
#include <string>

#include "envoy/config/filter/http/squash/v2/squash.pb.h"
#include "envoy/http/async_client.h"
#include "envoy/http/filter.h"
#include "envoy/upstream/cluster_manager.h"

#include "common/common/logger.h"
#include "common/protobuf/protobuf.h"

#include "absl/types/optional.h"

namespace Envoy {
namespace Http {

class SquashFilterConfig : protected Logger::Loggable<Logger::Id::config> {
public:
  SquashFilterConfig(const envoy::config::filter::http::squash::v2::Squash& proto_config,
                     Upstream::ClusterManager& clusterManager);
  const std::string& clusterName() { return cluster_name_; }
  const std::string& attachmentJson() { return attachment_json_; }
  const std::chrono::milliseconds& attachmentTimeout() { return attachment_timeout_; }
  const std::chrono::milliseconds& attachmentPollPeriod() { return attachment_poll_period_; }
  const std::chrono::milliseconds& requestTimeout() { return request_timeout_; }

private:
  // Get the attachment body, and returns a JSON representations with envrionment variables
  // interpolated.
  static std::string getAttachment(const ProtobufWkt::Struct& attachment_template);
  // Recursively interpolates envrionment variables inline in the struct.
  static void updateTemplateInStruct(ProtobufWkt::Struct& attachment_template);
  // Recursively interpolates envrionment variables inline in the value.
  static void updateTemplateInValue(ProtobufWkt::Value& curvalue);
  // Interpolates envrionment variables in a string, and returns the new interpolated string.
  static std::string replaceEnv(const std::string& attachment_template);

  // The name of the squash server cluster.
  std::string cluster_name_;
  // The attachment body sent to squash server on create attachment.
  std::string attachment_json_;
  // The total amount of time for an attachment to reach a final state (attached or error).
  std::chrono::milliseconds attachment_timeout_;
  // How frequently should we poll the attachment state with the squash server.
  std::chrono::milliseconds attachment_poll_period_;
  // The timeout for individual requests to the squash server.
  std::chrono::milliseconds request_timeout_;

  // Defines the pattern for interpolating envrionment variables in to the attachment.
  const static std::regex ENV_REGEX;
};

typedef std::shared_ptr<SquashFilterConfig> SquashFilterConfigSharedPtr;

class AsyncClientCallbackShim : public AsyncClient::Callbacks {
public:
  AsyncClientCallbackShim(std::function<void(MessagePtr&&)>&& on_success,
                          std::function<void(AsyncClient::FailureReason)>&& on_fail)
      : on_success_(on_success), on_fail_(on_fail) {}
  // Http::AsyncClient::Callbacks
  void onSuccess(MessagePtr&& m) override { on_success_(std::forward<MessagePtr>(m)); }
  void onFailure(AsyncClient::FailureReason f) override { on_fail_(f); }

private:
  std::function<void(MessagePtr&&)> on_success_;
  std::function<void(AsyncClient::FailureReason)> on_fail_;
};

class SquashFilter : public StreamDecoderFilter, protected Logger::Loggable<Logger::Id::filter> {
public:
  SquashFilter(SquashFilterConfigSharedPtr config, Upstream::ClusterManager& cm);
  ~SquashFilter();

  // Http::StreamFilterBase
  void onDestroy() override;

  // Http::StreamDecoderFilter
  FilterHeadersStatus decodeHeaders(HeaderMap& headers, bool) override;
  FilterDataStatus decodeData(Buffer::Instance&, bool) override;
  FilterTrailersStatus decodeTrailers(HeaderMap&) override;
  void setDecoderFilterCallbacks(StreamDecoderFilterCallbacks& callbacks) override;

private:
  // AsyncClient callbacks for create attachment request
  void onCreateAttachmentSuccess(MessagePtr&&);
  void onCreateAttachmentFailure(AsyncClient::FailureReason);
  // AsyncClient callbacks for get attachment request
  void onGetAttachmentSuccess(MessagePtr&&);
  void onGetAttachmentFailure(AsyncClient::FailureReason);

  // Schedules a pollForAttachment
  void scheduleRetry();
  // Contacts Squash server to get the latest version of a debug attachment.
  void pollForAttachment();
  // Cleanup and continue the filter chain.
  void doneSquashing();
  void cleanup();
  // Creates a JSON from the message body.
  Json::ObjectSharedPtr getJsonBody(MessagePtr&& m);

  const SquashFilterConfigSharedPtr config_;

  // Current state of the squash filter. If is_squashing_ is true, Hold the request while we
  // communicate with the squash server to attach a debugger. If it is false, let the the request
  // pass-through.
  bool is_squashing_;
  // The API path of the created debug attachment (used for polling its state).
  std::string debug_attachment_path_;
  // A timer for polling the state of a debug attachment until it reaches a final state.
  Event::TimerPtr attachment_poll_period_timer_;
  // A timeout timer - after this timer expires we abort polling the debug attachment, and continue
  // filter iteration
  Event::TimerPtr attachment_timeout_timer_;
  // The current inflight request to the squash server.
  AsyncClient::Request* in_flight_request_;
  // Shims to get AsyncClient callbacks to specific methods, per API method.
  AsyncClientCallbackShim create_attachment_callback_;
  AsyncClientCallbackShim check_attachment_callback_;

  // ClusterManager to send requests to squash server
  Upstream::ClusterManager& cm_;
  // Callbacks used to continue filter iteration.
  StreamDecoderFilterCallbacks* decoder_callbacks_;

  // Create debug attachment URL path.
  const static std::string POST_ATTACHMENT_PATH;
  // Authority header for squash server.
  const static std::string SERVER_AUTHORITY;
  // The state of a debug attachment object when a debugger is successfully attached.
  const static std::string ATTACHED_STATE;
  // The state of a debug attachment object when an error has occured.
  const static std::string ERROR_STATE;
};

} // namespace Http
} // namespace Envoy
