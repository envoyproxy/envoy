#include "source/common/buffer/buffer_impl.h"

#include "contrib/kafka/filters/network/source/broker/filter_config.h"
#include "contrib/kafka/filters/network/source/broker/rewriter.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {
namespace Broker {

static void putBytesIntoBuffer(Buffer::Instance& buffer, const uint32_t size) {
  std::vector<char> data(size, 42);
  absl::string_view sv = {data.data(), data.size()};
  buffer.add(sv);
}

static Buffer::InstancePtr makeRandomBuffer(const uint32_t size) {
  Buffer::InstancePtr result = std::make_unique<Buffer::OwnedImpl>();
  putBytesIntoBuffer(*result, size);
  return result;
}

class FakeResponse : public AbstractResponse {
public:
  FakeResponse(const size_t size) : AbstractResponse{{0, 0, 0}}, size_{size} {}

  uint32_t computeSize() const override { return size_; };

  virtual uint32_t encode(Buffer::Instance& dst) const override {
    putBytesIntoBuffer(dst, size_);
    return size_;
  };

private:
  size_t size_;
};

TEST(ResponseRewriterImplUnitTest, ShouldRewriteBuffer) {
  // given
  ResponseRewriterImpl testee;

  auto response1 = std::make_shared<FakeResponse>(7);
  auto response2 = std::make_shared<FakeResponse>(13);
  auto response3 = std::make_shared<FakeResponse>(42);

  // when - 1
  testee.onMessage(response1);
  testee.onMessage(response2);
  testee.onMessage(response3);

  // then - 1
  ASSERT_EQ(testee.getStoredResponseCountForTest(), 3);

  // when - 2
  auto buffer = makeRandomBuffer(4242);
  testee.process(*buffer);

  // then - 2
  ASSERT_EQ(testee.getStoredResponseCountForTest(), 0);
  ASSERT_EQ(buffer->length(), (3 * 4) + 7 + 13 + 42); // 4 bytes for message length
}

TEST(ResponseRewriterUnitTest, ShouldCreateProperRewriter) {
  ResponseRewriterSharedPtr r1 = createRewriter({"aaa", true});
  ASSERT_NE(std::dynamic_pointer_cast<ResponseRewriterImpl>(r1), nullptr);
  ResponseRewriterSharedPtr r2 = createRewriter({"aaa", false});
  ASSERT_NE(std::dynamic_pointer_cast<DoNothingRewriter>(r2), nullptr);
}

} // namespace Broker
} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
