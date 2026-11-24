#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <optional>
#include <string>

#include "envoy/data/ai/v3/request_info.pb.h"
#include "envoy/data/ai/v3/token_usage.pb.h"
#include "envoy/event/dispatcher.h"
#include "envoy/extensions/filters/http/ai_protocol_manager/v3/ai_protocol_manager.pb.h"
#include "envoy/http/codes.h"

#include "source/extensions/filters/http/ai_protocol_manager/ai_filter.h"
#include "source/extensions/filters/http/ai_protocol_manager/external_buffer_impl.h"
#include "source/extensions/filters/http/ai_protocol_manager/filter.h"

#include "test/common/stats/stat_test_utility.h"
#include "test/mocks/http/mocks.h"

#include "absl/strings/string_view.h"
#include "gmock/gmock.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiProtocolManager {

constexpr size_t kMaxInputSize = 64 * 1024;

using AiProtocolManagerProto =
    envoy::extensions::filters::http::ai_protocol_manager::v3::AiProtocolManager;

class FuzzRunner {
public:
  struct Result {
    Http::TestRequestHeaderMapImpl headers;
    std::string body;
    bool end_stream{false};
    int local_reply_calls{0};
    Http::Code local_reply_code{};
    std::optional<envoy::data::ai::v3::RequestInfo> published_request_info;
    std::optional<envoy::data::ai::v3::TokenUsage> published_usage;
  };

  FuzzRunner();

  Stats::Scope& scope() { return *stats_store_.rootScope(); }

  void createFilter(const RouteConfig& route_config, const AiProtocolManagerProto& filter_proto,
                    const AiFilterFactories& ai_filter_factories,
                    uint32_t decoder_buffer_limit = 0);

  void decodeHeaders(Http::TestRequestHeaderMapImpl headers);
  void decodeBody(absl::string_view body, uint64_t seed = 0, bool fragmented = false);
  void encodeHeaders(Http::TestResponseHeaderMapImpl headers);
  void encodeBody(absl::string_view body, uint64_t seed = 0, bool fragmented = false);
  Result finish();

  static void verifyEquivalent(const Result& fragmented, const Result& whole);

private:
  void runPostedCallbacks();

  Stats::TestUtil::TestStore stats_store_;
  ExternalBufferFactorySharedPtr buffer_factory_{std::make_shared<InMemoryExternalBufferFactory>()};
  testing::NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks_;
  testing::NiceMock<Http::MockStreamEncoderFilterCallbacks> encoder_callbacks_;
  const RouteConfig* route_config_{nullptr};
  uint32_t decoder_buffer_limit_{0};
  std::deque<Event::PostCb> posted_callbacks_;
  std::unique_ptr<AiProtocolManagerFilter> filter_;
  Result result_;
};

} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
