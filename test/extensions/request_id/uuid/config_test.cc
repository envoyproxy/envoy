#include <string>

#include "common/common/random_generator.h"

#include "extensions/request_id/uuid/config.h"

#include "test/mocks/common.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

using testing::Return;

namespace Envoy {
namespace Extensions {
namespace RequestId {

TEST(UUIDRequestIDExtensionTest, SetRequestID) {
  testing::StrictMock<Random::MockRandomGenerator> random;
  UUIDRequestIDExtension uuid_utils(envoy::extensions::request_id::uuid::v3::UuidRequestIdConfig(),
                                    random);
  Http::TestRequestHeaderMapImpl request_headers;

  EXPECT_CALL(random, uuid()).WillOnce(Return("first-request-id"));
  uuid_utils.set(request_headers, true);
  EXPECT_EQ("first-request-id", request_headers.get_(Http::Headers::get().RequestId));

  EXPECT_CALL(random, uuid()).WillOnce(Return("second-request-id"));
  uuid_utils.set(request_headers, true);
  EXPECT_EQ("second-request-id", request_headers.get_(Http::Headers::get().RequestId));
}

TEST(UUIDRequestIDExtensionTest, EnsureRequestID) {
  testing::StrictMock<Random::MockRandomGenerator> random;
  UUIDRequestIDExtension uuid_utils(envoy::extensions::request_id::uuid::v3::UuidRequestIdConfig(),
                                    random);
  Http::TestRequestHeaderMapImpl request_headers;

  EXPECT_CALL(random, uuid()).WillOnce(Return("first-request-id"));
  uuid_utils.set(request_headers, false);
  EXPECT_EQ("first-request-id", request_headers.get_(Http::Headers::get().RequestId));

  EXPECT_CALL(random, uuid()).Times(0);
  uuid_utils.set(request_headers, false);
  EXPECT_EQ("first-request-id", request_headers.get_(Http::Headers::get().RequestId));
}

TEST(UUIDRequestIDExtensionTest, PreserveRequestIDInResponse) {
  testing::StrictMock<Random::MockRandomGenerator> random;
  UUIDRequestIDExtension uuid_utils(envoy::extensions::request_id::uuid::v3::UuidRequestIdConfig(),
                                    random);
  Http::TestRequestHeaderMapImpl request_headers;
  Http::TestResponseHeaderMapImpl response_headers;

  uuid_utils.setInResponse(response_headers, request_headers);
  EXPECT_TRUE(response_headers.get(Http::Headers::get().RequestId).empty());

  request_headers.setRequestId("some-request-id");
  uuid_utils.setInResponse(response_headers, request_headers);
  EXPECT_EQ("some-request-id", response_headers.get_(Http::Headers::get().RequestId));

  request_headers.removeRequestId();
  response_headers.setRequestId("another-request-id");
  uuid_utils.setInResponse(response_headers, request_headers);
  EXPECT_EQ("another-request-id", response_headers.get_(Http::Headers::get().RequestId));

  request_headers.setRequestId("");
  uuid_utils.setInResponse(response_headers, request_headers);
  EXPECT_EQ("", response_headers.get_(Http::Headers::get().RequestId));
}

TEST(UUIDRequestIDExtensionTest, ModRequestIDBy) {
  Random::RandomGeneratorImpl random;
  UUIDRequestIDExtension uuid_utils(envoy::extensions::request_id::uuid::v3::UuidRequestIdConfig(),
                                    random);
  Http::TestRequestHeaderMapImpl request_headers;

  uint64_t result;
  EXPECT_FALSE(uuid_utils.modBy(request_headers, result, 10000));

  request_headers.setRequestId("fffffff");
  EXPECT_FALSE(uuid_utils.modBy(request_headers, result, 10000));

  request_headers.setRequestId("fffffffz-0012-0110-00ff-0c00400600ff");
  EXPECT_FALSE(uuid_utils.modBy(request_headers, result, 10000));

  request_headers.setRequestId("00000000-0000-0000-0000-000000000000");
  EXPECT_TRUE(uuid_utils.modBy(request_headers, result, 100));
  EXPECT_EQ(0, result);

  request_headers.setRequestId("00000001-0000-0000-0000-000000000000");
  EXPECT_TRUE(uuid_utils.modBy(request_headers, result, 100));
  EXPECT_EQ(1, result);

  request_headers.setRequestId("0000000f-0000-0000-0000-00000000000a");
  EXPECT_TRUE(uuid_utils.modBy(request_headers, result, 100));
  EXPECT_EQ(15, result);

  request_headers.setRequestId("");
  EXPECT_FALSE(uuid_utils.modBy(request_headers, result, 100));

  request_headers.setRequestId("000000ff-0000-0000-0000-000000000000");
  EXPECT_TRUE(uuid_utils.modBy(request_headers, result, 100));
  EXPECT_EQ(55, result);

  request_headers.setRequestId("000000ff-0000-0000-0000-000000000000");
  EXPECT_TRUE(uuid_utils.modBy(request_headers, result, 10000));
  EXPECT_EQ(255, result);

  request_headers.setRequestId("a0090100-0012-0110-00ff-0c00400600ff");
  EXPECT_TRUE(uuid_utils.modBy(request_headers, result, 137));
  EXPECT_EQ(8, result);

  request_headers.setRequestId("ffffffff-0012-0110-00ff-0c00400600ff");
  EXPECT_TRUE(uuid_utils.modBy(request_headers, result, 100));
  EXPECT_EQ(95, result);

  request_headers.setRequestId("ffffffff-0012-0110-00ff-0c00400600ff");
  EXPECT_TRUE(uuid_utils.modBy(request_headers, result, 10000));
  EXPECT_EQ(7295, result);
}

TEST(UUIDRequestIDExtensionTest, RequestIDModDistribution) {
  Random::RandomGeneratorImpl random;
  UUIDRequestIDExtension uuid_utils(envoy::extensions::request_id::uuid::v3::UuidRequestIdConfig(),
                                    random);
  Http::TestRequestHeaderMapImpl request_headers;

  const int mod = 100;
  const int required_percentage = 11;
  int total_samples = 0;
  int interesting_samples = 0;

  for (int i = 0; i < 500000; ++i) {
    std::string uuid = random.uuid();

    const char c = uuid[19];
    ASSERT_TRUE(uuid[14] == '4');                              // UUID version 4 (random)
    ASSERT_TRUE(c == '8' || c == '9' || c == 'a' || c == 'b'); // UUID variant 1 (RFC4122)

    uint64_t value;
    request_headers.setRequestId(uuid);
    ASSERT_TRUE(uuid_utils.modBy(request_headers, value, mod));

    if (value < required_percentage) {
      interesting_samples++;
    }
    total_samples++;
  }

  EXPECT_NEAR(required_percentage / 100.0, interesting_samples * 1.0 / total_samples, 0.002);
}

TEST(UUIDRequestIDExtensionTest, DISABLED_benchmark) {
  Random::RandomGeneratorImpl random;

  for (int i = 0; i < 100000000; ++i) {
    random.uuid();
  }
}

TEST(UUIDRequestIDExtensionTest, SetTraceStatus) {
  Random::RandomGeneratorImpl random;
  UUIDRequestIDExtension uuid_utils(envoy::extensions::request_id::uuid::v3::UuidRequestIdConfig(),
                                    random);
  Http::TestRequestHeaderMapImpl request_headers;
  request_headers.setRequestId(random.uuid());

  EXPECT_EQ(Tracing::Reason::NotTraceable, uuid_utils.getTraceReason(request_headers));

  uuid_utils.setTraceReason(request_headers, Tracing::Reason::Sampling);
  EXPECT_EQ(Tracing::Reason::Sampling, uuid_utils.getTraceReason(request_headers));

  uuid_utils.setTraceReason(request_headers, Tracing::Reason::ClientForced);
  EXPECT_EQ(Tracing::Reason::ClientForced, uuid_utils.getTraceReason(request_headers));

  uuid_utils.setTraceReason(request_headers, Tracing::Reason::ServiceForced);
  EXPECT_EQ(Tracing::Reason::ServiceForced, uuid_utils.getTraceReason(request_headers));

  uuid_utils.setTraceReason(request_headers, Tracing::Reason::NotTraceable);
  EXPECT_EQ(Tracing::Reason::NotTraceable, uuid_utils.getTraceReason(request_headers));

  // Invalid request ID.
  request_headers.setRequestId("");
  uuid_utils.setTraceReason(request_headers, Tracing::Reason::ServiceForced);
  EXPECT_EQ(request_headers.getRequestIdValue(), "");
}

TEST(UUIDRequestIDExtensionTest, SetTraceStatusPackingDisabled) {
  Random::RandomGeneratorImpl random;
  envoy::extensions::request_id::uuid::v3::UuidRequestIdConfig config;
  config.mutable_pack_trace_reason()->set_value(false);
  UUIDRequestIDExtension uuid_utils(config, random);

  Http::TestRequestHeaderMapImpl request_headers;
  request_headers.setRequestId(random.uuid());
  EXPECT_EQ(Tracing::Reason::NotTraceable, uuid_utils.getTraceReason(request_headers));

  uuid_utils.setTraceReason(request_headers, Tracing::Reason::Sampling);
  EXPECT_EQ(Tracing::Reason::NotTraceable, uuid_utils.getTraceReason(request_headers));
}

} // namespace RequestId
} // namespace Extensions
} // namespace Envoy
