#include "source/common/network/address_impl.h"

#include "test/extensions/filters/http/common/fuzz/http_filter_fuzzer.h"
#include "test/fuzz/fuzz_runner.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/runtime/mocks.h"

#include "contrib/envoy/extensions/filters/http/golang/v3alpha/golang.pb.validate.h"
#include "contrib/golang/filters/http/source/golang_filter.h"
#include "contrib/golang/filters/http/test/common/dso/mocks.h"
#include "contrib/golang/filters/http/test/golang_filter_fuzz.pb.validate.h"
#include "gmock/gmock.h"

using testing::Return;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Golang {
namespace {

class FuzzerMocks {
public:
  FuzzerMocks() : addr_(std::make_shared<Network::Address::PipeInstance>("/test/test.sock")) {

    ON_CALL(decoder_callbacks_, connection())
        .WillByDefault(Return(OptRef<const Network::Connection>{connection_}));
    connection_.stream_info_.downstream_connection_info_provider_->setRemoteAddress(addr_);
    connection_.stream_info_.downstream_connection_info_provider_->setLocalAddress(addr_);
  }

  NiceMock<Runtime::MockLoader> runtime_;
  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks_;
  NiceMock<Http::MockStreamEncoderFilterCallbacks> encoder_callbacks_;
  Network::Address::InstanceConstSharedPtr addr_;
  NiceMock<Envoy::Network::MockConnection> connection_;
};

DEFINE_PROTO_FUZZER(const envoy::extensions::filters::http::golang::GolangFilterTestCase& input) {
  try {
    TestUtility::validate(input);
  } catch (const EnvoyException& e) {
    ENVOY_LOG_MISC(debug, "EnvoyException during validation: {}", e.what());
    return;
  }

  auto dso_lib = std::make_shared<Dso::MockDsoInstance>();

  // hard code the return config_id to 1 since the default 0 is invalid.
  ON_CALL(*dso_lib.get(), envoyGoFilterNewHttpPluginConfig(_, _)).WillByDefault(Return(1));
  ON_CALL(*dso_lib.get(), envoyGoFilterOnHttpHeader(_, _, _, _))
      .WillByDefault(Return(static_cast<uint64_t>(GolangStatus::Continue)));
  ON_CALL(*dso_lib.get(), envoyGoFilterOnHttpData(_, _, _, _))
      .WillByDefault(Return(static_cast<uint64_t>(GolangStatus::Continue)));

  static FuzzerMocks mocks;

  // Prepare filter.
  const envoy::extensions::filters::http::golang::v3alpha::Config& proto_config = input.config();
  FilterConfigSharedPtr config;

  try {
    config = std::make_shared<FilterConfig>(proto_config, dso_lib);
  } catch (const EnvoyException& e) {
    ENVOY_LOG_MISC(debug, "EnvoyException during filter config validation: {}", e.what());
    return;
  }

  std::unique_ptr<Filter> filter = std::make_unique<Filter>(config, dso_lib);
  filter->setDecoderFilterCallbacks(mocks.decoder_callbacks_);
  filter->setEncoderFilterCallbacks(mocks.encoder_callbacks_);

  Envoy::Extensions::HttpFilters::HttpFilterFuzzer fuzzer;
  fuzzer.runData(static_cast<Envoy::Http::StreamDecoderFilter*>(filter.get()),
                 input.request_data());
}

} // namespace
} // namespace Golang
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
