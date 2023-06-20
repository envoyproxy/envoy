#include "test/extensions/filters/http/common/fuzz/uber_filter.h"

#include "source/common/common/thread_impl.h"
#include "source/common/config/utility.h"
#include "source/common/event/dispatcher_impl.h"
#include "source/common/http/message_impl.h"
#include "source/common/http/utility.h"
#include "source/common/protobuf/protobuf.h"
#include "source/common/protobuf/utility.h"

#include "test/test_common/utility.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {

UberFilterFuzzer::UberFilterFuzzer()
    : async_request_{&cluster_manager_.thread_local_cluster_.async_client_},
      thread_factory_(Thread::threadFactoryForTest()) {
  ON_CALL(api_, threadFactory()).WillByDefault(testing::ReturnRef(thread_factory_));
  worker_thread_dispatcher_ =
      std::make_unique<Event::DispatcherImpl>("filter_fuzz_test", api_, api_.time_system_);
  // This is a decoder filter.
  ON_CALL(filter_callback_, addStreamDecoderFilter(_))
      .WillByDefault(Invoke([&](Http::StreamDecoderFilterSharedPtr filter) -> void {
        decoder_filter_ = filter;
        decoder_filter_->setDecoderFilterCallbacks(decoder_callbacks_);
      }));
  // This is an encoded filter.
  ON_CALL(filter_callback_, addStreamEncoderFilter(_))
      .WillByDefault(Invoke([&](Http::StreamEncoderFilterSharedPtr filter) -> void {
        encoder_filter_ = filter;
        encoder_filter_->setEncoderFilterCallbacks(encoder_callbacks_);
      }));
  // This is a decoder and encoder filter.
  ON_CALL(filter_callback_, addStreamFilter(_))
      .WillByDefault(Invoke([&](Http::StreamFilterSharedPtr filter) -> void {
        decoder_filter_ = filter;
        decoder_filter_->setDecoderFilterCallbacks(decoder_callbacks_);
        encoder_filter_ = filter;
        encoder_filter_->setEncoderFilterCallbacks(encoder_callbacks_);
      }));
  // This filter supports access logging.
  ON_CALL(filter_callback_, addAccessLogHandler(_))
      .WillByDefault(
          Invoke([&](AccessLog::InstanceSharedPtr handler) -> void { access_logger_ = handler; }));
  // This handles stopping execution after a direct response is sent.
  ON_CALL(decoder_callbacks_, sendLocalReply(_, _, _, _, _))
      .WillByDefault(
          Invoke([this](Http::Code code, absl::string_view body,
                        std::function<void(Http::ResponseHeaderMap & headers)> modify_headers,
                        const absl::optional<Grpc::Status::GrpcStatus> grpc_status,
                        absl::string_view details) {
            enabled_ = false;
            decoder_callbacks_.sendLocalReply_(code, body, modify_headers, grpc_status, details);
          }));
  ON_CALL(encoder_callbacks_, addEncodedTrailers())
      .WillByDefault(testing::ReturnRef(encoded_trailers_));
  // Set expectations for particular filters that may get fuzzed.
  perFilterSetup();
}

void UberFilterFuzzer::fuzz(
    const envoy::extensions::filters::network::http_connection_manager::v3::HttpFilter&
        proto_config,
    const test::fuzz::HttpData& downstream_data, const test::fuzz::HttpData& upstream_data) {
  try {
    // Try to create the filter. Exit early if the config is invalid or violates PGV constraints.
    ENVOY_LOG_MISC(info, "filter name {}", proto_config.name());
    auto& factory = Config::Utility::getAndCheckFactoryByName<
        Server::Configuration::NamedHttpFilterConfigFactory>(proto_config.name());
    ProtobufTypes::MessagePtr message = Config::Utility::translateToFactoryConfig(
        proto_config, factory_context_.messageValidationVisitor(), factory);
    // Clean-up config with filter-specific logic before it runs through validations.
    cleanFuzzedConfig(proto_config.name(), message.get());
    cb_ = factory.createFilterFactoryFromProto(*message, "stats", factory_context_);
    cb_(filter_callback_);
  } catch (const EnvoyException& e) {
    ENVOY_LOG_MISC(debug, "Controlled exception {}", e.what());
    return;
  }

  // Data path should not throw exceptions.
  if (decoder_filter_ != nullptr) {
    HttpFilterFuzzer::runData(decoder_filter_.get(), downstream_data);
  } else {
    decoding_finished_ = true;
  }
  if (encoder_filter_ != nullptr) {
    HttpFilterFuzzer::runData(encoder_filter_.get(), upstream_data);
  } else {
    encoding_finished_ = true;
  }
  if (access_logger_ != nullptr) {
    HttpFilterFuzzer::accessLog(access_logger_.get(), stream_info_);
  }

  // Most filters should have finished processing during runData, but filters that
  // rely on an additional thread (e.g. for file system interaction) may need to wait
  // for the worker thread to complete the filter's task.
  // We can't use a time-based timeout for this, as lint forbids use of clock time,
  // and fake time isn't useful for allowing other threads to complete work.
  int loop_cycles = 5000;
  while (!isFilterFinished() && --loop_cycles > 0) {
    worker_thread_dispatcher_->run(Event::DispatcherImpl::RunType::NonBlock);
    // Apparently RunType::Block doesn't actually block if there's only a timer
    // event not ready to fire, so we use NonBlock for clarity, and this loop
    // spins. We yield to keep it from spinning too wildly. For almost all cases
    // this shouldn't matter as even entering the loop at all is rare, and for
    // the cases where the loop is useful, a few cycles should be enough to
    // complete unless the test is genuinely failing.
    std::this_thread::yield();
  }
  if (!isFilterFinished()) {
    throw EnvoyException("filter did not finish processing");
  }

  reset();
}

void UberFilterFuzzer::reset() {
  if (decoder_filter_ != nullptr) {
    decoder_filter_->onDestroy();
  }
  decoder_filter_.reset();

  if (encoder_filter_ != nullptr) {
    encoder_filter_->onDestroy();
  }
  encoder_filter_.reset();

  access_logger_.reset();
  custom_stat_namespaces_ = Stats::CustomStatNamespacesImpl();
  decoding_buffer_ = nullptr;
  HttpFilterFuzzer::reset();
}

} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
