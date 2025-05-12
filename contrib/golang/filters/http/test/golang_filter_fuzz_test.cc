#include "source/common/network/address_impl.h"

#include "test/extensions/filters/http/common/fuzz/http_filter_fuzzer.h"
#include "test/fuzz/fuzz_runner.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/server/factory_context.h"

#include "contrib/envoy/extensions/filters/http/golang/v3alpha/golang.pb.validate.h"
#include "contrib/golang/common/dso/test/mocks.h"
#include "contrib/golang/filters/http/source/golang_filter.h"
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
  FuzzerMocks() : addr_(*Network::Address::PipeInstance::create("/test/test.sock")) {

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

  auto dso_lib = std::make_shared<Dso::MockHttpFilterDsoImpl>();

  // hard code the return config_id to 1 since the default 0 is invalid.
  ON_CALL(*dso_lib.get(), envoyGoFilterNewHttpPluginConfig(_)).WillByDefault(Return(1));
  ON_CALL(*dso_lib.get(), envoyGoFilterOnHttpHeader(_, _, _, _))
      .WillByDefault(Return(static_cast<uint64_t>(GolangStatus::Continue)));
  ON_CALL(*dso_lib.get(), envoyGoFilterOnHttpData(_, _, _, _))
      .WillByDefault(Return(static_cast<uint64_t>(GolangStatus::Continue)));
  ON_CALL(*dso_lib.get(), envoyGoFilterOnHttpLog(_, _, _, _, _, _, _, _, _, _, _, _))
      .WillByDefault(
          Invoke([&](httpRequest*, int, processState*, processState*, GoUint64, GoUint64, GoUint64,
                     GoUint64, GoUint64, GoUint64, GoUint64, GoUint64) -> void {}));
  ON_CALL(*dso_lib.get(), envoyGoFilterOnHttpStreamComplete(_))
      .WillByDefault(Invoke([&](httpRequest*) -> void {}));
  ON_CALL(*dso_lib.get(), envoyGoFilterOnHttpDestroy(_, _))
      .WillByDefault(Invoke([&](httpRequest* p0, int) -> void {
        // delete the filter->req_, make LeakSanitizer happy.
        auto req = reinterpret_cast<HttpRequestInternal*>(p0);
        delete req;
      }));

  static FuzzerMocks mocks;

  // Filter config is typically considered trusted (coming from a trusted domain), use a const
  // config is good enough.
  const auto yaml = R"EOF(
    library_id: test
    library_path: test
    plugin_name: test
    )EOF";

  envoy::extensions::filters::http::golang::v3alpha::Config proto_config;
  TestUtility::loadFromYaml(yaml, proto_config);

  // Prepare filter.
  NiceMock<Server::Configuration::MockFactoryContext> context;
  FilterConfigSharedPtr config = std::make_shared<FilterConfig>(proto_config, dso_lib, "", context);
  std::unique_ptr<Filter> filter = std::make_unique<Filter>(config, dso_lib, 0);
  filter->setDecoderFilterCallbacks(mocks.decoder_callbacks_);
  filter->setEncoderFilterCallbacks(mocks.encoder_callbacks_);

  Envoy::Extensions::HttpFilters::HttpFilterFuzzer fuzzer;
  fuzzer.runData(static_cast<Envoy::Http::StreamDecoderFilter*>(filter.get()),
                 input.request_data());
  filter->onDestroy();
}

} // namespace
} // namespace Golang
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
