#include <optional>

#include "contrib/kafka/filters/network/source/external/responses.h"
#include "contrib/kafka/filters/network/source/mesh/command_handlers/api_versions.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {
namespace Mesh {
namespace {

class MockAbstractRequestListener : public AbstractRequestListener {
public:
  MOCK_METHOD(void, onRequest, (InFlightRequestSharedPtr));
  MOCK_METHOD(void, onRequestReadyForAnswer, ());
  MOCK_METHOD(Event::Dispatcher&, dispatcher, ());
};

TEST(ApiVersionsTest, shouldBeAlwaysReadyForAnswer) {
  // given
  MockAbstractRequestListener filter;
  EXPECT_CALL(filter, onRequestReadyForAnswer());
  const RequestHeader header = {API_VERSIONS_REQUEST_API_KEY, 0, 0, std::nullopt};
  ApiVersionsRequestHolder testee = {filter, header};

  // when, then - invoking should immediately notify the filter.
  testee.startProcessing();

  // when, then - should always be considered finished.
  const bool finished = testee.finished();
  EXPECT_TRUE(finished);

  // when, then - the computed result is always contains correct data (confirmed by integration
  // tests).
  const auto answer = testee.computeAnswer();
  EXPECT_EQ(answer->metadata_.api_key_, header.api_key_);
  EXPECT_EQ(answer->metadata_.correlation_id_, header.correlation_id_);
}

// The mesh must advertise only API versions that its generated codec can actually parse. If it
// advertised a version outside the generated range, a client could negotiate a version the filter
// cannot handle (e.g. Kafka 4.x removed Fetch v0-3). This locks the advertised range to the codec
// the handler was built against, so it cannot silently rely on - or outrun - that Kafka version.
TEST(ApiVersionsTest, shouldAdvertiseOnlyVersionsTheCodecCanParse) {
  // given
  MockAbstractRequestListener filter;
  EXPECT_CALL(filter, onRequestReadyForAnswer());
  const RequestHeader header = {API_VERSIONS_REQUEST_API_KEY, 0, 0, std::nullopt};
  ApiVersionsRequestHolder testee = {filter, header};
  testee.startProcessing();

  // when
  const auto answer = testee.computeAnswer();
  const auto response = std::dynamic_pointer_cast<Response<ApiVersionsResponse>>(answer);
  ASSERT_NE(nullptr, response);

  // then - every advertised entry is a non-empty range within the generated codec's version range.
  const auto codec_min = [](const int16_t api_key) -> std::optional<int16_t> {
    switch (api_key) {
    case PRODUCE_REQUEST_API_KEY:
      return PRODUCE_REQUEST_MIN_VERSION;
    case FETCH_REQUEST_API_KEY:
      return FETCH_REQUEST_MIN_VERSION;
    case LIST_OFFSETS_REQUEST_API_KEY:
      return LIST_OFFSETS_REQUEST_MIN_VERSION;
    case METADATA_REQUEST_API_KEY:
      return METADATA_REQUEST_MIN_VERSION;
    default:
      return std::nullopt;
    }
  };
  const auto codec_max = [](const int16_t api_key) -> std::optional<int16_t> {
    switch (api_key) {
    case PRODUCE_REQUEST_API_KEY:
      return PRODUCE_REQUEST_MAX_VERSION;
    case FETCH_REQUEST_API_KEY:
      return FETCH_REQUEST_MAX_VERSION;
    case LIST_OFFSETS_REQUEST_API_KEY:
      return LIST_OFFSETS_REQUEST_MAX_VERSION;
    case METADATA_REQUEST_API_KEY:
      return METADATA_REQUEST_MAX_VERSION;
    default:
      return std::nullopt;
    }
  };

  EXPECT_FALSE(response->data_.api_keys_.empty());
  for (const auto& entry : response->data_.api_keys_) {
    EXPECT_LE(entry.min_version_, entry.max_version_);
    const auto min_bound = codec_min(entry.api_key_);
    const auto max_bound = codec_max(entry.api_key_);
    ASSERT_TRUE(min_bound.has_value());
    ASSERT_TRUE(max_bound.has_value());
    // never advertise a version below what the codec can parse ...
    EXPECT_GE(entry.min_version_, *min_bound);
    // ... nor above it.
    EXPECT_LE(entry.max_version_, *max_bound);
  }
}

} // namespace
} // namespace Mesh
} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
