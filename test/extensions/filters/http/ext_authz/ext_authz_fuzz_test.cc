#include "envoy/extensions/filters/http/ext_authz/v3/ext_authz.pb.validate.h"

#include "common/http/context_impl.h"
#include "common/network/address_impl.h"

#include "extensions/filters/http/ext_authz/ext_authz.h"

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
makeAuthzResponse(Filters::Common::ExtAuthz::CheckStatus status) {
  Filters::Common::ExtAuthz::ResponsePtr response =
      std::make_unique<Filters::Common::ExtAuthz::Response>();
  response->status = status;
  // TODO: add headers to remove, append, set, body, status_code.
  return response;
}

Filters::Common::ExtAuthz::CheckStatus resultCaseToCheckStatus(
    envoy::extensions::filters::http::ext_authz::Result::ResultSelectorCase result_case) {
  Filters::Common::ExtAuthz::CheckStatus check_status;
  switch (result_case) {
  case envoy::extensions::filters::http::ext_authz::Result::kOk: {
    check_status = Filters::Common::ExtAuthz::CheckStatus::OK;
    break;
  }
  case envoy::extensions::filters::http::ext_authz::Result::kError: {
    check_status = Filters::Common::ExtAuthz::CheckStatus::Error;
    break;
  }
  case envoy::extensions::filters::http::ext_authz::Result::kDenied: {
    check_status = Filters::Common::ExtAuthz::CheckStatus::Denied;
    break;
  }
  default: {
    // Unhandled status.
    NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
  }
  }
  return check_status;
}

class FuzzerMocks {
public:
  FuzzerMocks() : addr_(std::make_shared<Network::Address::PipeInstance>("/test/test.sock")) {

    ON_CALL(decoder_callbacks_, connection()).WillByDefault(Return(&connection_));
    connection_.stream_info_.downstream_address_provider_->setRemoteAddress(addr_);
    connection_.stream_info_.downstream_address_provider_->setLocalAddress(addr_);
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
  Http::ContextImpl http_context(stats_store.symbolTable());
  Filters::Common::ExtAuthz::MockClient* client = new Filters::Common::ExtAuthz::MockClient();

  // Prepare filter.
  envoy::extensions::filters::http::ext_authz::v3::ExtAuthz proto_config = input.config();
  FilterConfigSharedPtr config = std::make_shared<FilterConfig>(
      proto_config, stats_store, mocks.runtime_, http_context, "ext_authz_prefix");
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
        callbacks.onComplete(
            makeAuthzResponse(resultCaseToCheckStatus(input.result().result_selector_case())));
      }));

  // TODO: Add response headers.
  Envoy::Extensions::HttpFilters::HttpFilterFuzzer fuzzer;
  fuzzer.runData(static_cast<Envoy::Http::StreamDecoderFilter*>(filter.get()), input.data());
  // TODO: Query the request header map in HttpFilterFuzzer to test headers_to_(add/remove/append).
  // TODO: Test check request attributes against config and filter metadata.
  ENVOY_LOG_MISC(trace, "Check Request attributes {}", check_request.attributes().DebugString());
}

} // namespace
} // namespace ExtAuthz
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
