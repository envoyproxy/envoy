#include "test/extensions/filters/http/ext_authz/ext_authz_fuzz_lib.h"

#include "envoy/config/core/v3/base.pb.h"

#include "source/common/network/address_impl.h"
#include "source/extensions/filters/http/ext_authz/ext_authz.h"

#include "test/extensions/filters/http/ext_authz/ext_authz_fuzz.pb.h"
#include "test/mocks/network/mocks.h"

#include "gmock/gmock.h"

using testing::Return;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExtAuthz {

ReusableFilterFactory::ReusableFilterFactory()
    : addr_(*Network::Address::PipeInstance::create("/test/test.sock")) {
  connection_.stream_info_.downstream_connection_info_provider_->setRemoteAddress(addr_);
  connection_.stream_info_.downstream_connection_info_provider_->setLocalAddress(addr_);

  ON_CALL(decoder_callbacks_, connection())
      .WillByDefault(Return(OptRef<const Network::Connection>{connection_}));
  ON_CALL(decoder_callbacks_.stream_info_, dynamicMetadata())
      .WillByDefault(testing::ReturnRef(metadata_));
  ON_CALL(decoder_callbacks_, decodingBuffer()).WillByDefault([this]() {
    return decoding_buffer_.get();
  });
}

std::unique_ptr<Filter>
ReusableFilterFactory::newFilter(FilterConfigSharedPtr config,
                                 Filters::Common::ExtAuthz::ClientPtr&& client,
                                 const envoy::config::core::v3::Metadata& metadata) {
  decoding_buffer_ = std::make_unique<Buffer::OwnedImpl>();
  metadata_ = metadata;
  decoder_callbacks_.stream_info_.filter_state_ =
      std::make_shared<StreamInfo::FilterStateImpl>(StreamInfo::FilterState::LifeSpan::FilterChain);

  std::unique_ptr<Filter> filter = std::make_unique<Filter>(std::move(config), std::move(client));
  filter->setDecoderFilterCallbacks(decoder_callbacks_);
  filter->setEncoderFilterCallbacks(encoder_callbacks_);

  return filter;
}

absl::StatusOr<std::unique_ptr<Filter>> ReusableFuzzerUtil::setup(
    const envoy::extensions::filters::http::ext_authz::ExtAuthzTestCaseBase& input,
    Filters::Common::ExtAuthz::ClientPtr client) {

  // Prepare filter.
  const envoy::extensions::filters::http::ext_authz::v3::ExtAuthz proto_config = input.config();
  FilterConfigSharedPtr config;

  try {
    config = std::make_shared<FilterConfig>(proto_config, *stats_store_.rootScope(),
                                            "ext_authz_prefix", factory_context_);
  } catch (const EnvoyException& e) {
    ENVOY_LOG_MISC(debug, "EnvoyException during filter config validation: {}", e.what());
    return absl::InvalidArgumentError(
        absl::StrCat("EnvoyException during filter config validation: {}", e.what()));
  }

  return filter_factory_.newFilter(std::move(config), std::move(client), input.filter_metadata());
}

} // namespace ExtAuthz
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
