#include "source/extensions/filters/http/ext_proc/ext_proc.h"

#include "test/extensions/filters/http/common/fuzz/http_filter_fuzzer.h"
#include "test/extensions/filters/http/ext_proc/unit_test_fuzz/ext_proc_unit_test_fuzz.pb.validate.h"
#include "test/extensions/filters/http/ext_proc/unit_test_fuzz/mocks.h"
#include "test/fuzz/fuzz_runner.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/network/mocks.h"

using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExtProc {
namespace UnitTestFuzz {

class FuzzerMocks {
public:
  FuzzerMocks() : addr_(std::make_shared<Network::Address::PipeInstance>("/test/test.sock")) {
    ON_CALL(decoder_callbacks_, connection())
        .WillByDefault(Return(OptRef<const Network::Connection>{connection_}));
    connection_.stream_info_.downstream_connection_info_provider_->setRemoteAddress(addr_);
    connection_.stream_info_.downstream_connection_info_provider_->setLocalAddress(addr_);
    ON_CALL(decoder_callbacks_, addDecodedTrailers()).WillByDefault(ReturnRef(request_trailers_));
    ON_CALL(encoder_callbacks_, addEncodedTrailers()).WillByDefault(ReturnRef(response_trailers_));
  }

  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks_;
  NiceMock<Http::MockStreamEncoderFilterCallbacks> encoder_callbacks_;
  Network::Address::InstanceConstSharedPtr addr_;
  NiceMock<Envoy::Network::MockConnection> connection_;
  NiceMock<Http::TestRequestTrailerMapImpl> request_trailers_;
  NiceMock<Http::TestResponseTrailerMapImpl> response_trailers_;
};

DEFINE_PROTO_FUZZER(
    const envoy::extensions::filters::http::ext_proc::unit_test_fuzz::ExtProcUnitTestCase& input) {
  try {
    TestUtility::validate(input);
  } catch (const EnvoyException& e) {
    ENVOY_LOG_MISC(debug, "EnvoyException during validation: {}", e.what());
    return;
  }
  static FuzzerMocks mocks;
  NiceMock<Stats::MockIsolatedStatsStore> stats_store;

  // Prepare filter.
  const envoy::extensions::filters::http::ext_proc::v3::ExternalProcessor proto_config =
      input.config();
  ExternalProcessing::FilterConfigSharedPtr config;

  // Create regex engine which is used by regex matcher code.
  Regex::EnginePtr regex_engine = std::make_shared<Regex::GoogleReEngine>();
  Regex::EngineSingleton::clear();
  Regex::EngineSingleton::initialize(regex_engine.get());

  try {
    config = std::make_shared<ExternalProcessing::FilterConfig>(
        proto_config, std::chrono::milliseconds(200), 200, *stats_store.rootScope(),
        "ext_proc_prefix");
  } catch (const EnvoyException& e) {
    ENVOY_LOG_MISC(debug, "EnvoyException during ext_proc filter config validation: {}", e.what());
    return;
  }

  MockClient* client = new MockClient();
  std::unique_ptr<ExternalProcessing::Filter> filter = std::make_unique<ExternalProcessing::Filter>(
      config, ExternalProcessing::ExternalProcessorClientPtr{client}, proto_config.grpc_service());
  filter->setDecoderFilterCallbacks(mocks.decoder_callbacks_);
  filter->setEncoderFilterCallbacks(mocks.encoder_callbacks_);

  ON_CALL(*client, start(_, _, _))
      .WillByDefault(Invoke(
          [&](ExternalProcessing::ExternalProcessorCallbacks&,
              const envoy::config::core::v3::GrpcService&,
              const StreamInfo::StreamInfo&) -> ExternalProcessing::ExternalProcessorStreamPtr {
            auto stream = std::make_unique<MockStream>();
            EXPECT_CALL(*stream, send(_, _))
                .WillRepeatedly(
                    Invoke([&](envoy::service::ext_proc::v3::ProcessingRequest&&, bool) -> void {
                      auto response =
                          std::make_unique<envoy::service::ext_proc::v3::ProcessingResponse>(
                              input.response());
                      filter->onReceiveMessage(std::move(response));
                    }));
            ON_CALL(*stream, close()).WillByDefault(Return(false));
            return stream;
          }));

  Envoy::Extensions::HttpFilters::HttpFilterFuzzer fuzzer;
  fuzzer.runData(static_cast<Envoy::Http::StreamDecoderFilter*>(filter.get()), input.request());
}

} // namespace UnitTestFuzz
} // namespace ExtProc
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
