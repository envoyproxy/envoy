#include <memory>

#include "envoy/extensions/filters/http/api_key_auth/v3/api_key_auth.pb.h"
#include "envoy/extensions/filters/http/api_key_auth/v3/api_key_auth.pb.validate.h"

#include "source/common/common/regex.h"
#include "source/common/http/message_impl.h"
#include "source/common/router/string_accessor_impl.h"
#include "source/extensions/filters/http/api_key_auth/api_key_auth.h"

#include "test/extensions/filters/http/api_key_auth/api_key_auth_fuzz.pb.h"
#include "test/extensions/filters/http/api_key_auth/api_key_auth_fuzz.pb.validate.h"
#include "test/extensions/filters/http/common/fuzz/http_filter_fuzzer.h"
#include "test/fuzz/fuzz_runner.h"
#include "test/mocks/server/factory_context.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ApiKeyAuth {
namespace {

using envoy::extensions::filters::http::api_key_auth::ApiKeyAuthFuzzInput;
using testing::NiceMock;

DEFINE_PROTO_FUZZER(const ApiKeyAuthFuzzInput& input) {
  try {
    TestUtility::validate(input);
  } catch (const EnvoyException& e) {
    ENVOY_LOG_MISC(debug, "EnvoyException during validation: {}", e.what());
    return;
  }

  NiceMock<Server::Configuration::MockFactoryContext> mock_factory_ctx;
  NiceMock<Http::MockStreamDecoderFilterCallbacks> filter_callbacks;

  // Mock per route config.
  std::unique_ptr<RouteConfig> route_config;
  ON_CALL(filter_callbacks, mostSpecificPerFilterConfig()).WillByDefault(Invoke([&]() {
    return route_config.get();
  }));

  absl::Status filter_config_creation_status = absl::OkStatus();
  std::shared_ptr<FilterConfig> filter_config = std::make_shared<FilterConfig>(
      input.filter_config(), mock_factory_ctx.scope_, "stats.", filter_config_creation_status);

  if (!filter_config_creation_status.ok()) {
    ENVOY_LOG_MISC(debug, "EnvoyException during filter config construction: {}",
                   filter_config_creation_status.message());
  }

  // Simulate multiple calls to execute jwt_cache and jwks_cache codes
  auto filter = std::make_unique<ApiKeyAuthFilter>(filter_config);
  filter->setDecoderFilterCallbacks(filter_callbacks);

  HttpFilterFuzzer fuzzer;
  fuzzer.runData(static_cast<Http::StreamDecoderFilter*>(filter.get()), input.request_data());

  filter->onDestroy();

  fuzzer.reset();
}

} // namespace
} // namespace ApiKeyAuth
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
