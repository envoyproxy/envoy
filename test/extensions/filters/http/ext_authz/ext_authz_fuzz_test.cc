#include "envoy/extensions/filters/http/ext_authz/v3/ext_authz.pb.validate.h"

#include "source/common/http/context_impl.h"
#include "source/common/network/address_impl.h"
#include "source/extensions/filters/http/ext_authz/ext_authz.h"

#include "test/extensions/filters/common/ext_authz/mocks.h"
#include "test/extensions/filters/http/common/fuzz/http_filter_fuzzer.h"
#include "test/extensions/filters/http/ext_authz/ext_authz_fuzz.pb.validate.h"
#include "test/fuzz/fuzz_runner.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/runtime/mocks.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExtAuthz {
namespace {

Filters::Common::ExtAuthz::ResponsePtr
makeAuthzResponse(const Filters::Common::ExtAuthz::CheckStatus status) {
  Filters::Common::ExtAuthz::ResponsePtr response =
      std::make_unique<Filters::Common::ExtAuthz::Response>();
  response->status = status;
  // TODO: We only add the response status.
  // Add fuzzed inputs for headers_to_(set/append/add), body, status_code to the Response.
  return response;
}

Filters::Common::ExtAuthz::CheckStatus resultCaseToCheckStatus(
    const envoy::extensions::filters::http::ext_authz::ExtAuthzTestCase::AuthResult result) {
  Filters::Common::ExtAuthz::CheckStatus check_status;
  switch (result) {
  case envoy::extensions::filters::http::ext_authz::ExtAuthzTestCase::OK: {
    check_status = Filters::Common::ExtAuthz::CheckStatus::OK;
    break;
  }
  case envoy::extensions::filters::http::ext_authz::ExtAuthzTestCase::ERROR: {
    check_status = Filters::Common::ExtAuthz::CheckStatus::Error;
    break;
  }
  case envoy::extensions::filters::http::ext_authz::ExtAuthzTestCase::DENIED: {
    check_status = Filters::Common::ExtAuthz::CheckStatus::Denied;
    break;
  }
  default: {
    // Unhandled status.
    PANIC("not implemented");
  }
  }
  return check_status;
}

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

DEFINE_PROTO_FUZZER(const envoy::extensions::filters::http::ext_authz::ExtAuthzTestCase& input) {
  try {
    TestUtility::validate(input);
  } catch (const EnvoyException& e) {
    ENVOY_LOG_MISC(debug, "EnvoyException during validation: {}", e.what());
    return;
  }

  static FuzzerMocks mocks;
  NiceMock<Stats::MockIsolatedStatsStore> stats_store;
  static ScopedInjectableLoader<Regex::Engine> engine(std::make_unique<Regex::GoogleReEngine>());
  envoy::config::bootstrap::v3::Bootstrap bootstrap;
  Http::ContextImpl http_context(stats_store.symbolTable());

  // Prepare filter.
  const envoy::extensions::filters::http::ext_authz::v3::ExtAuthz proto_config = input.config();
  FilterConfigSharedPtr config;

  try {
    config = std::make_shared<FilterConfig>(proto_config, stats_store, mocks.runtime_, http_context,
                                            "ext_authz_prefix", bootstrap);
  } catch (const EnvoyException& e) {
    ENVOY_LOG_MISC(debug, "EnvoyException during filter config validation: {}", e.what());
    return;
  }

  Filters::Common::ExtAuthz::MockClient* client = new Filters::Common::ExtAuthz::MockClient();
  std::unique_ptr<Filter> filter =
      std::make_unique<Filter>(config, Filters::Common::ExtAuthz::ClientPtr{client});
  filter->setDecoderFilterCallbacks(mocks.decoder_callbacks_);
  filter->setEncoderFilterCallbacks(mocks.encoder_callbacks_);

  // Set metadata context.
  envoy::config::core::v3::Metadata metadata = input.filter_metadata();

  ON_CALL(mocks.decoder_callbacks_.stream_info_, dynamicMetadata())
      .WillByDefault(testing::ReturnRef(metadata));
  // Set check result default action.
  envoy::service::auth::v3::CheckRequest check_request;
  ON_CALL(*client, check(_, _, _, _))
      .WillByDefault(Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks& callbacks,
                                const envoy::service::auth::v3::CheckRequest& check_param,
                                Tracing::Span&, const StreamInfo::StreamInfo&) -> void {
        check_request = check_param;
        callbacks.onComplete(makeAuthzResponse(resultCaseToCheckStatus(input.result())));
      }));

  // TODO: Add response headers.
  Envoy::Extensions::HttpFilters::HttpFilterFuzzer fuzzer;
  fuzzer.runData(static_cast<Envoy::Http::StreamDecoderFilter*>(filter.get()),
                 input.request_data());
  // TODO: Query the request header map in HttpFilterFuzzer to test headers_to_(add/remove/append).
  // TODO: Test check request attributes against config and filter metadata.
  ENVOY_LOG_MISC(trace, "Check Request attributes {}", check_request.attributes().DebugString());
}

} // namespace
} // namespace ExtAuthz
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
