#include <array>
#include <string>
#include <vector>

#include "source/common/http/http1/balsa_parser.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Http {
namespace Http1 {
namespace {

constexpr char kValidHttpTokenCharacters[] =
    "!#$%&'*+-.0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ^_`abcdefghijklmnopqrstuvwxyz|~";

class RecordingCallbacks : public ParserCallbacks {
public:
  CallbackResult onMessageBegin() override { return CallbackResult::Success; }
  CallbackResult onUrl(const char*, size_t) override { return CallbackResult::Success; }
  CallbackResult onStatus(const char*, size_t) override { return CallbackResult::Success; }
  CallbackResult onHeaderField(const char* data, size_t length) override {
    header_names_.emplace_back(data, length);
    return CallbackResult::Success;
  }
  CallbackResult onHeaderValue(const char*, size_t) override { return CallbackResult::Success; }
  CallbackResult onHeadersComplete() override { return CallbackResult::Success; }
  void bufferBody(const char*, size_t) override {}
  CallbackResult onMessageComplete() override { return CallbackResult::Success; }
  void onChunkHeader(bool) override {}

  const std::vector<std::string>& headerNames() const { return header_names_; }

private:
  std::vector<std::string> header_names_;
};

TEST(BalsaParserHeaderNameValidationTest, ParsesHeaderNameContainingEveryTokenCharacter) {
  std::string request = "GET / HTTP/1.1\r\n";
  request += kValidHttpTokenCharacters;
  request += ": value\r\n\r\n";

  RecordingCallbacks callbacks;
  BalsaParser parser(MessageType::Request, &callbacks, request.size(), false, false);

  EXPECT_EQ(request.size(), parser.execute(request.data(), request.size()));
  EXPECT_EQ(ParserStatus::Ok, parser.getStatus()) << parser.errorMessage();
  ASSERT_EQ(1, callbacks.headerNames().size());
  EXPECT_EQ(kValidHttpTokenCharacters, callbacks.headerNames()[0]);
}

TEST(BalsaParserHeaderNameValidationTest, RejectsInvalidTokenCharactersInHeaderNames) {
  constexpr std::array<unsigned char, 21> invalid_characters = {
      0x00, ' ', '"', '(',  ')', ',', '/', ';',  '<',  '=', '>',
      '?',  '@', '[', '\\', ']', '{', '}', 0x7f, 0x80, 0xff};

  for (const unsigned char c : invalid_characters) {
    SCOPED_TRACE(testing::Message() << "character code " << static_cast<int>(c));
    std::string request = "GET / HTTP/1.1\r\nbad";
    request.push_back(static_cast<char>(c));
    request += "name: value\r\n\r\n";

    RecordingCallbacks callbacks;
    BalsaParser parser(MessageType::Request, &callbacks, request.size(), false, false);

    parser.execute(request.data(), request.size());

    EXPECT_EQ(ParserStatus::Error, parser.getStatus());
    EXPECT_FALSE(parser.errorMessage().empty());
    EXPECT_TRUE(callbacks.headerNames().empty());
  }
}

TEST(BalsaParserHeaderNameValidationTest, RejectsHighBitHeaderNameCharacterAfterParsing) {
  std::string request = "GET / HTTP/1.1\r\nf";
  request.push_back(static_cast<char>(0xc3));
  request.push_back(static_cast<char>(0xb6));
  request += "o: value\r\n\r\n";

  RecordingCallbacks callbacks;
  BalsaParser parser(MessageType::Request, &callbacks, request.size(), false, false);

  parser.execute(request.data(), request.size());

  EXPECT_EQ(ParserStatus::Error, parser.getStatus());
  EXPECT_EQ("HPE_INVALID_HEADER_TOKEN", parser.errorMessage());
  EXPECT_TRUE(callbacks.headerNames().empty());
}

TEST(BalsaParserMethodValidationTest, ParsesCustomMethodContainingEveryTokenCharacter) {
  std::string request;
  request += kValidHttpTokenCharacters;
  request += " / HTTP/1.1\r\nhost: example.com\r\n\r\n";

  RecordingCallbacks callbacks;
  BalsaParser parser(MessageType::Request, &callbacks, request.size(), false, true);

  EXPECT_EQ(request.size(), parser.execute(request.data(), request.size()));
  EXPECT_EQ(ParserStatus::Ok, parser.getStatus()) << parser.errorMessage();
  EXPECT_EQ(kValidHttpTokenCharacters, parser.methodName());
}

} // namespace
} // namespace Http1
} // namespace Http
} // namespace Envoy
