#include <iostream>
#include <string>

#include "envoy/common/scope_tracker.h"
#include "envoy/http/filter.h"
#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

#include "source/common/common/scope_tracker.h"
#include "source/extensions/filters/http/common/factory_base.h"
#include "source/extensions/filters/http/common/pass_through_filter.h"

#include "test/integration/filters/common.h"
#include "test/integration/filters/stop_and_continue_filter_config.pb.h"
#include "test/integration/filters/stop_and_continue_filter_config.pb.validate.h"
#include "test/test_common/utility.h"

namespace Envoy {

// A test filter that does StopIterationNoBuffer on end stream, then continues after a 0ms alarm.
// It can optionally register a ScopeTrackedObject on continuation.
class StopIterationAndContinueFilter : public Http::PassThroughFilter {
public:
  StopIterationAndContinueFilter(bool set_tracked_object, bool stop_and_buffer)
      : set_tracked_object_(set_tracked_object), stop_and_buffer_(stop_and_buffer) {}

  void setEndStreamAndDecodeTimer() {
    decode_end_stream_seen_ = true;
    decode_delay_timer_ = decoder_callbacks_->dispatcher().createTimer([this]() -> void {
      absl::optional<MessageTrackedObject> msg;
      absl::optional<ScopeTrackerScopeState> state;
      if (set_tracked_object_) {
        msg.emplace("StopIterationAndContinue decode_delay_timer");
        state.emplace(&msg.value(), decoder_callbacks_->dispatcher());
      }
      decoder_callbacks_->continueDecoding();
    });
    decode_delay_timer_->enableTimer(std::chrono::seconds(0));
  }

  void setEndStreamAndEncodeTimer() {
    encode_end_stream_seen_ = true;
    encode_delay_timer_ = decoder_callbacks_->dispatcher().createTimer([this]() -> void {
      absl::optional<MessageTrackedObject> msg;
      absl::optional<ScopeTrackerScopeState> state;
      if (set_tracked_object_) {
        msg.emplace("StopIterationAndContinue encode_delay_timer");
        state.emplace(&msg.value(), decoder_callbacks_->dispatcher());
      }
      encoder_callbacks_->continueEncoding();
    });
    encode_delay_timer_->enableTimer(std::chrono::seconds(0));
  }

  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap&, bool end_stream) override {
    if (end_stream) {
      setEndStreamAndDecodeTimer();
      return Http::FilterHeadersStatus::StopIteration;
    }
    return Http::FilterHeadersStatus::Continue;
  }

  Http::FilterDataStatus decodeData(Buffer::Instance&, bool end_stream) override {
    RELEASE_ASSERT(!decode_end_stream_seen_, "end stream seen twice");
    if (end_stream) {
      setEndStreamAndDecodeTimer();
    }
    if (stop_and_buffer_) {
      return Http::FilterDataStatus::StopIterationAndBuffer;
    }
    return Http::FilterDataStatus::StopIterationNoBuffer;
  }

  Http::FilterHeadersStatus encodeHeaders(Http::ResponseHeaderMap&, bool end_stream) override {
    if (end_stream) {
      setEndStreamAndEncodeTimer();
      return Http::FilterHeadersStatus::StopIteration;
    }
    return Http::FilterHeadersStatus::Continue;
  }

  Http::FilterDataStatus encodeData(Buffer::Instance&, bool end_stream) override {
    RELEASE_ASSERT(!encode_end_stream_seen_, "end stream seen twice");
    if (end_stream) {
      setEndStreamAndEncodeTimer();
    }
    return Http::FilterDataStatus::StopIterationNoBuffer;
  }

  Event::TimerPtr decode_delay_timer_;
  bool decode_end_stream_seen_{};
  Event::TimerPtr encode_delay_timer_;
  bool encode_end_stream_seen_{};
  bool set_tracked_object_{};
  bool stop_and_buffer_{};
};

class StopIterationAndContinueFilterFactory
    : public Extensions::HttpFilters::Common::FactoryBase<
          test::integration::filters::StopAndContinueConfig> {
public:
  StopIterationAndContinueFilterFactory() : FactoryBase("stop-iteration-and-continue-filter") {}

private:
  Http::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const test::integration::filters::StopAndContinueConfig& proto_config, const std::string&,
      Server::Configuration::FactoryContext&) override {
    bool set_scope_tacked_object = proto_config.install_scope_tracked_object();
    bool stop_and_buffer = proto_config.stop_and_buffer();
    return [set_scope_tacked_object,
            stop_and_buffer](Http::FilterChainFactoryCallbacks& callbacks) -> void {
      callbacks.addStreamFilter(std::make_shared<::Envoy::StopIterationAndContinueFilter>(
          set_scope_tacked_object, stop_and_buffer));
    };
  }
};

class StopIterationAndContinueUpstreamFilterFactory
    : public Server::Configuration::UpstreamHttpFilterConfigFactory {
public:
  std::string name() const override { return "stop-iteration-and-continue-filter"; }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<test::integration::filters::StopAndContinueConfig>();
  }

  absl::StatusOr<Http::FilterFactoryCb>
  createFilterFactoryFromProto(const Protobuf::Message& config, const std::string&,
                               Server::Configuration::UpstreamFactoryContext& context) override {

    const auto& proto_config =
        MessageUtil::downcastAndValidate<const test::integration::filters::StopAndContinueConfig&>(
            config, context.serverFactoryContext().messageValidationVisitor());

    return [proto_config](Http::FilterChainFactoryCallbacks& callbacks) -> void {
      callbacks.addStreamFilter(std::make_shared<StopIterationAndContinueFilter>(
          proto_config.install_scope_tracked_object(), proto_config.stop_and_buffer()));
    };
  };
};

static Registry::RegisterFactory<StopIterationAndContinueFilterFactory,
                                 Server::Configuration::NamedHttpFilterConfigFactory>
    register_;
static Registry::RegisterFactory<StopIterationAndContinueUpstreamFilterFactory,
                                 Server::Configuration::UpstreamHttpFilterConfigFactory>
    register_upstream_;

} // namespace Envoy
