#pragma once

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/network/address.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/extensions/filters/http/ext_authz/ext_authz.h"

#include "test/extensions/filters/http/ext_authz/ext_authz_fuzz.pb.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/server/server_factory_context.h"
#include "test/mocks/stats/mocks.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExtAuthz {

class ReusableFilterFactory {
public:
  ReusableFilterFactory();

  // Update metadata_ and create a filter using the config and client.
  std::unique_ptr<Filter> newFilter(FilterConfigSharedPtr config,
                                    Filters::Common::ExtAuthz::ClientPtr&& client,
                                    const envoy::config::core::v3::Metadata& metadata);

private:
  // Do not use ON_CALL outside of constructor on these mocks. Each ON_CALL has a memory cost and
  // will cause OOMs if the fuzzer runs long enough.
  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks_;
  std::unique_ptr<Buffer::OwnedImpl> decoding_buffer_;
  NiceMock<Http::MockStreamEncoderFilterCallbacks> encoder_callbacks_;
  Network::Address::InstanceConstSharedPtr addr_;
  NiceMock<Envoy::Network::MockConnection> connection_;

  // Returned by decoder_callbacks.stream_info_.dynamicMetadata(). Updated by calling newFilter.
  envoy::config::core::v3::Metadata metadata_;
};

class ReusableFuzzerUtil {
public:
  // Validate input, then create a filter using the input.config() & the provided client.
  absl::StatusOr<std::unique_ptr<Filter>>
  setup(const envoy::extensions::filters::http::ext_authz::ExtAuthzTestCaseBase& input,
        Filters::Common::ExtAuthz::ClientPtr client);

private:
  NiceMock<Stats::MockIsolatedStatsStore> stats_store_;
  NiceMock<Server::Configuration::MockServerFactoryContext> factory_context_;
  ReusableFilterFactory filter_factory_;
};

} // namespace ExtAuthz
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
