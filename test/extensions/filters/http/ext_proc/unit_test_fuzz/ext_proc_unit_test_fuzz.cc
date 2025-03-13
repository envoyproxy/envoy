#include "source/extensions/filters/http/ext_proc/ext_proc.h"

#include "test/extensions/filters/http/common/fuzz/http_filter_fuzzer.h"
#include "test/extensions/filters/http/ext_proc/mock_server.h"
#include "test/extensions/filters/http/ext_proc/unit_test_fuzz/ext_proc_unit_test_fuzz.pb.validate.h"
#include "test/fuzz/fuzz_runner.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/server/server_factory_context.h"

using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExtProc {
namespace UnitTestFuzz {

class FuzzerMocks {
public:
  FuzzerMocks()
      : addr_(*Network::Address::PipeInstance::create("/test/test.sock")), buffer_("foo") {
    ON_CALL(decoder_callbacks_, connection())
        .WillByDefault(Return(OptRef<const Network::Connection>{connection_}));
    connection_.stream_info_.downstream_connection_info_provider_->setRemoteAddress(addr_);
    connection_.stream_info_.downstream_connection_info_provider_->setLocalAddress(addr_);
    ON_CALL(decoder_callbacks_, addDecodedTrailers()).WillByDefault(ReturnRef(request_trailers_));
    ON_CALL(encoder_callbacks_, addEncodedTrailers()).WillByDefault(ReturnRef(response_trailers_));
    ON_CALL(decoder_callbacks_, decodingBuffer()).WillByDefault(Return(&buffer_));
    ON_CALL(encoder_callbacks_, encodingBuffer()).WillByDefault(Return(&buffer_));
    ON_CALL(decoder_callbacks_, decoderBufferLimit()).WillByDefault(Return(1024));
    ON_CALL(encoder_callbacks_, encoderBufferLimit()).WillByDefault(Return(1024));
    ON_CALL(decoder_callbacks_, injectDecodedDataToFilterChain(_, _))
        .WillByDefault(
            Invoke([&](Buffer::Instance& data, bool) -> void { data.drain(data.length()); }));
    ON_CALL(encoder_callbacks_, injectEncodedDataToFilterChain(_, _))
        .WillByDefault(
            Invoke([&](Buffer::Instance& data, bool) -> void { data.drain(data.length()); }));
  }

  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks_;
  NiceMock<Http::MockStreamEncoderFilterCallbacks> encoder_callbacks_;
  Network::Address::InstanceConstSharedPtr addr_;
  NiceMock<Envoy::Network::MockConnection> connection_;
  NiceMock<Http::TestRequestTrailerMapImpl> request_trailers_;
  NiceMock<Http::TestResponseTrailerMapImpl> response_trailers_;
  NiceMock<Buffer::OwnedImpl> buffer_;
  NiceMock<StreamInfo::MockStreamInfo> async_client_stream_info_;
  NiceMock<Server::Configuration::MockServerFactoryContext> factory_context_;
};

DEFINE_PROTO_FUZZER(
    const envoy::extensions::filters::http::ext_proc::unit_test_fuzz::ExtProcUnitTestCase& input) {
  try {
    TestUtility::validate(input);
  } catch (const EnvoyException& e) {
    ENVOY_LOG_MISC(debug, "EnvoyException during validation: {}", e.what());
    return;
  }

  // ASAN fuzz testing is producing an error on CEL parsing that is believed to be a false positive.
  // It is determined that the error is unrelated to the implementation of request and response
  // attributes as demonstrated here
  // https://github.com/envoyproxy/envoy/compare/main...jbohanon:envoy:bug/cel-parser-antlr-exception-use-after-free.
  // Discussion on this is located at https://github.com/envoyproxy/envoy/pull/31017
  if (!input.config().request_attributes().empty() ||
      !input.config().response_attributes().empty()) {
    return;
  }

  // Limiting the max supported request body size to 128k.
  if (input.request().has_proto_body()) {
    const uint32_t max_body_size = 128 * 1024;
    if (input.request().proto_body().message().value().size() > max_body_size) {
      return;
    }
  }

  static FuzzerMocks mocks;
  NiceMock<Stats::MockIsolatedStatsStore> stats_store;

  // Prepare filter.
  const envoy::extensions::filters::http::ext_proc::v3::ExternalProcessor proto_config =
      input.config();
  ExternalProcessing::FilterConfigSharedPtr config;

  try {
    config = std::make_shared<ExternalProcessing::FilterConfig>(
        proto_config, std::chrono::milliseconds(200), 200, *stats_store.rootScope(), "", false,
        std::make_shared<Envoy::Extensions::Filters::Common::Expr::BuilderInstance>(
            Envoy::Extensions::Filters::Common::Expr::createBuilder(nullptr)),
        mocks.factory_context_);
  } catch (const EnvoyException& e) {
    ENVOY_LOG_MISC(debug, "EnvoyException during ext_proc filter config validation: {}", e.what());
    return;
  }

  ExternalProcessing::MockClient* client = new ExternalProcessing::MockClient();
  std::unique_ptr<ExternalProcessing::Filter> filter = std::make_unique<ExternalProcessing::Filter>(
      config, ExternalProcessing::ExternalProcessorClientPtr{client});
  filter->setDecoderFilterCallbacks(mocks.decoder_callbacks_);
  filter->setEncoderFilterCallbacks(mocks.encoder_callbacks_);

  EXPECT_CALL(*client, start(_, _, _, _))
      .WillRepeatedly(Invoke([&](ExternalProcessing::ExternalProcessorCallbacks&,
                                 const Grpc::GrpcServiceConfigWithHashKey&,
                                 const Envoy::Http::AsyncClient::StreamOptions&,
                                 Envoy::Http::StreamFilterSidestreamWatermarkCallbacks&)
                                 -> ExternalProcessing::ExternalProcessorStreamPtr {
        auto stream = std::make_unique<ExternalProcessing::MockStream>();
        EXPECT_CALL(*stream, send(_, _))
            .WillRepeatedly(Invoke([&](envoy::service::ext_proc::v3::ProcessingRequest&&,
                                       bool) -> void {
              auto response = std::make_unique<envoy::service::ext_proc::v3::ProcessingResponse>(
                  input.response());
              filter->onReceiveMessage(std::move(response));
            }));
        EXPECT_CALL(*stream, streamInfo())
            .WillRepeatedly(ReturnRef(mocks.async_client_stream_info_));
        EXPECT_CALL(*stream, close()).WillRepeatedly(Return(false));
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
