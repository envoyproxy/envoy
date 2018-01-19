#pragma once

#include <map>
#include <regex>
#include <string>

#include "envoy/common/optional.h"
#include "envoy/http/async_client.h"
#include "envoy/http/filter.h"
#include "envoy/upstream/cluster_manager.h"

#include "common/common/logger.h"
#include "common/protobuf/protobuf.h"

#include "api/filter/http/squash.pb.h"

namespace Envoy {
namespace Http {

class SquashFilterConfig : protected Logger::Loggable<Logger::Id::config> {
public:
  SquashFilterConfig(const envoy::api::v2::filter::http::Squash& proto_config,
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

  std::string cluster_name_;
  std::string attachment_json_;
  std::chrono::milliseconds attachment_timeout_;
  std::chrono::milliseconds attachment_poll_period_;
  std::chrono::milliseconds request_timeout_;

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
  // Current state of the squash filter:
  // NotSquashing - Let the the request pass-through.
  // Squashing - Hold the request while we communicate with the squash server to attach a debugger.
  enum class State {
    NotSquashing,
    Squashing,
  };

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

  State state_;
  std::string debugAttachmentPath_;
  Event::TimerPtr delay_timer_;
  Event::TimerPtr attachment_timeout_timer_;
  AsyncClient::Request* in_flight_request_;
  AsyncClientCallbackShim createAttachmentCallback_;
  AsyncClientCallbackShim checkAttachmentCallback_;

  Upstream::ClusterManager& cm_;
  StreamDecoderFilterCallbacks* decoder_callbacks_;

  const static std::string POST_ATTACHMENT_PATH;
  const static std::string SERVER_AUTHORITY;
  const static std::string ATTACHED_STATE;
  const static std::string ERROR_STATE;
};

} // namespace Http
} // namespace Envoy
