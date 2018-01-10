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
  static std::string getAttachment(const ProtobufWkt::Struct& attachment_template);
  static void getAttachmentFromStruct(ProtobufWkt::Struct& attachment_template);
  static void getAttachmentFromValue(ProtobufWkt::Value& curvalue);
  static std::string replaceEnv(const std::string& attachment_template);

  std::string cluster_name_;
  std::string attachment_json_;
  std::chrono::milliseconds attachment_timeout_;
  std::chrono::milliseconds attachment_poll_period_;
  std::chrono::milliseconds request_timeout_;

  const static std::regex ENV_REGEX;
};

typedef std::shared_ptr<SquashFilterConfig> SquashFilterConfigSharedPtr;

class SquashFilter : public StreamDecoderFilter,
                     protected Logger::Loggable<Logger::Id::filter>,
                     public AsyncClient::Callbacks {
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

  // Http::AsyncClient::Callbacks
  void onSuccess(MessagePtr&&) override;
  void onFailure(AsyncClient::FailureReason) override;

private:
  enum class State {
    Initial,
    CreateConfig,
    CheckAttachment,
  };

  void retry();
  void pollForAttachment();
  void doneSquashing();
  void cleanup();
  Json::ObjectSharedPtr getJsonBody(MessagePtr&& m);

  const SquashFilterConfigSharedPtr config_;

  State state_;
  std::string debugAttachmentPath_;
  Event::TimerPtr delay_timer_;
  Event::TimerPtr attachment_timeout_timer_;
  AsyncClient::Request* in_flight_request_;

  Upstream::ClusterManager& cm_;
  StreamDecoderFilterCallbacks* decoder_callbacks_;

  const static std::string POST_ATTACHMENT_PATH;
  const static std::string SERVER_AUTHORITY;
  const static std::string ATTACHED_STATE;
  const static std::string ERROR_STATE;
};

} // namespace Http
} // namespace Envoy
