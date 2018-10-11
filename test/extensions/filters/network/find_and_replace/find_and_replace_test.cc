#include "extensions/filters/network/find_and_replace/find_and_replace.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace FindAndReplace {

class FindAndReplaceFilterTest : public testing::Test {
public:
  void initialize() {
    const std::string config = R"EOF(
    input_rewrite_from: "PUT /test HTTP/1.1"
    input_rewrite_to: "GET /test HTTP/1.1\r\nConnection: Upgrade\r\nUpgrade: Websocket"
    output_rewrite_from: "HTTP/1.1 101 Switching Protocols\r\nconnection: upgrade\r\nupgrade: websocket"
    output_rewrite_to: "HTTP/1.1 200 OK"
    )EOF";
    Envoy::ProtobufWkt::Struct message;
    MessageUtil::loadFromYaml(config, message);
    config_ = std::make_shared<Config>(message);
    filter_ = std::make_unique<Filter>(config_);
  }

  ConfigConstSharedPtr config_;
  std::unique_ptr<Filter> filter_;
};

TEST_F(FindAndReplaceFilterTest, ReadBasicMatch) {
  initialize();

  std::string original;
  original += config_->input_rewrite_from();
  original += "\r\nfoobar";
  Buffer::OwnedImpl buf(original);

  filter_->onData(buf, false);
  std::string expected(config_->input_rewrite_to());
  expected += "\r\nfoobar";
  EXPECT_EQ(buf.search(expected.data(), expected.size(), 0), 0);
}

TEST_F(FindAndReplaceFilterTest, WriteBasicMatch) {
  initialize();

  std::string original;
  original += config_->output_rewrite_from();
  original += "\r\nfoobar";
  Buffer::OwnedImpl buf(original);

  filter_->onWrite(buf, false);
  std::string expected(config_->output_rewrite_to());
  expected += "\r\nfoobar";
  EXPECT_EQ(buf.search(expected.data(), expected.size(), 0), 0);
}

TEST_F(FindAndReplaceFilterTest, ReadFragmentedMatch) {
  initialize();

  std::string original;
  original += config_->input_rewrite_from();
  original += "\r\nfoobar";
  {
    std::string substr =
        config_->input_rewrite_from().substr(0, config_->input_rewrite_from().size() - 1);
    Buffer::OwnedImpl buf(substr);

    filter_->onData(buf, false);
    EXPECT_EQ(buf.search(substr.data(), substr.size(), 0), 0);
  }

  Buffer::OwnedImpl buf(original);
  filter_->onData(buf, false);
  std::string expected(config_->input_rewrite_to());
  expected += "\r\nfoobar";
  EXPECT_EQ(buf.search(expected.data(), expected.size(), 0), 0);
}

TEST_F(FindAndReplaceFilterTest, WriteFragmentedMatch) {
  initialize();

  std::string original;
  original += config_->output_rewrite_from();
  original += "\r\nfoobar";
  {
    std::string substr =
        config_->output_rewrite_from().substr(0, config_->output_rewrite_from().size() - 1);
    Buffer::OwnedImpl buf(substr);

    filter_->onWrite(buf, false);
    EXPECT_EQ(buf.search(substr.data(), substr.size(), 0), 0);
  }

  Buffer::OwnedImpl buf(original);
  filter_->onWrite(buf, false);
  std::string expected(config_->output_rewrite_to());
  expected += "\r\nfoobar";
  EXPECT_EQ(buf.search(expected.data(), expected.size(), 0), 0);
}

TEST_F(FindAndReplaceFilterTest, ReadPrefixNonMatch) {
  initialize();

  std::string original;
  original += "a";
  original += config_->input_rewrite_from();
  original += "\r\nfoobar";
  Buffer::OwnedImpl buf(original);

  filter_->onData(buf, false);
  EXPECT_EQ(buf.search(original.data(), original.size(), 0), 0);
}

TEST_F(FindAndReplaceFilterTest, WritePrefixNonMatch) {
  initialize();

  std::string original;
  original += "a";
  original += config_->output_rewrite_from();
  original += "\r\nfoobar";
  Buffer::OwnedImpl buf(original);

  filter_->onWrite(buf, false);
  EXPECT_EQ(buf.search(original.data(), original.size(), 0), 0);
}

TEST_F(FindAndReplaceFilterTest, ReadMiddleNonMatch) {
  initialize();

  std::string original;
  original += config_->input_rewrite_from();
  original[2] = ' ';
  original += "\r\nfoobar";
  Buffer::OwnedImpl buf(original);

  filter_->onData(buf, false);
  EXPECT_EQ(buf.search(original.data(), original.size(), 0), 0);
}

TEST_F(FindAndReplaceFilterTest, WriteMiddleNonMatch) {
  initialize();

  std::string original;
  original += config_->output_rewrite_from();
  original[2] = ' ';
  original += "\r\nfoobar";
  Buffer::OwnedImpl buf(original);

  filter_->onWrite(buf, false);
  EXPECT_EQ(buf.search(original.data(), original.size(), 0), 0);
}

} // namespace FindAndReplace
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
