#include "test/test_common/utility.h"

#include "contrib/mcp_sse_stateful_session/http/source/envelope.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Http {
namespace SseSessionState {
namespace Envelope {
namespace {

constexpr absl::string_view CRLFCRLF = "\r\n\r\n";
constexpr absl::string_view CRCR = "\r\r";
constexpr absl::string_view LFLF = "\n\n";
constexpr char SEPARATOR = '.'; // separate session ID and host address in sse mode

TEST(EnvelopeSessionStateFactoryTest, EnvelopeSessionStateTestOnUpdateDataSse) {
  EnvelopeSessionStateProto config;
  config.set_param_name("sessionId");
  config.add_chunk_end_patterns("\r\n\r\n");
  config.add_chunk_end_patterns("\n\n");
  config.add_chunk_end_patterns("\r\r");
  EnvelopeSessionStateFactory factory(config);
  Envoy::Http::TestRequestHeaderMapImpl request_headers;
  auto session_state = factory.create(request_headers);

  const std::string host_address = "1.2.3.4:80";
  const std::string raw_session_id = "abcdefg";

  // Base64Url encoded host address
  const std::string encoded_host =
      Envoy::Base64Url::encode(host_address.data(), host_address.size());
  const std::string session_value = raw_session_id + SEPARATOR + encoded_host;

  Buffer::OwnedImpl data_buffer;

  // Case 1: Incomplete chunk with valid Content-Type
  {
    Envoy::Http::TestResponseHeaderMapImpl headers{{":status", "200"},
                                                   {"content-type", "text/event-stream"}};

    session_state->onUpdateHeader(host_address, headers);

    // Add incomplete SSE data
    data_buffer.add("data: http://example.com?sessionId=abcdefg");

    // Call onUpdateData (this will move data to pending_chunk_)
    EXPECT_EQ(session_state->onUpdateData(host_address, data_buffer, false),
              Envoy::Http::FilterDataStatus::Continue);
    EXPECT_EQ(data_buffer.length(), 0); // data_buffer should be drained
  }

  // Case 2: Non-SSE response (Content-Type: text/plain)
  {
    Envoy::Http::TestResponseHeaderMapImpl headers{{":status", "200"},
                                                   {"content-type", "text/plain"}};

    session_state->onUpdateHeader(host_address, headers);

    data_buffer.add(absl::StrCat("data: http://example.com?sessionId=", raw_session_id, LFLF));

    EXPECT_EQ(session_state->onUpdateData(host_address, data_buffer, false),
              Envoy::Http::FilterDataStatus::Continue);

    const std::string result_data(
        static_cast<const char*>(data_buffer.linearize(data_buffer.length())),
        data_buffer.length());

    // Should NOT be modified
    EXPECT_NE(result_data.find("sessionId=abcdefg"), std::string::npos);
    EXPECT_EQ(result_data.find(encoded_host), std::string::npos);
    data_buffer.drain(data_buffer.length());
    EXPECT_FALSE(session_state->sessionIdFound());
    session_state->resetSessionIdFound();
  }

  // Case 3: Valid SSE response with LFLF \n\n
  {
    Envoy::Http::TestResponseHeaderMapImpl headers{{":status", "200"},
                                                   {"content-type", "text/event-stream"}};

    session_state->onUpdateHeader(host_address, headers);

    data_buffer.add(absl::StrCat("data: http://example.com?sessionId=", raw_session_id, LFLF));

    EXPECT_EQ(session_state->onUpdateData(host_address, data_buffer, false),
              Envoy::Http::FilterDataStatus::Continue);

    const std::string expected_url =
        absl::StrCat("http://example.com?sessionId=", raw_session_id, ".", encoded_host);

    const std::string result_data(
        static_cast<const char*>(data_buffer.linearize(data_buffer.length())),
        data_buffer.length());

    EXPECT_NE(result_data.find(expected_url), std::string::npos);
    data_buffer.drain(data_buffer.length());
    EXPECT_TRUE(session_state->sessionIdFound());
    session_state->resetSessionIdFound();
  }

  // Case 4: Valid SSE response with CRCR \r\r
  {
    Envoy::Http::TestResponseHeaderMapImpl headers{{":status", "200"},
                                                   {"content-type", "text/event-stream"}};

    session_state->onUpdateHeader(host_address, headers);

    data_buffer.add(absl::StrCat("data: http://example.com?sessionId=", raw_session_id, CRCR));

    EXPECT_EQ(session_state->onUpdateData(host_address, data_buffer, false),
              Envoy::Http::FilterDataStatus::Continue);

    const std::string expected_url =
        absl::StrCat("http://example.com?sessionId=", raw_session_id, ".", encoded_host);

    const std::string result_data(
        static_cast<const char*>(data_buffer.linearize(data_buffer.length())),
        data_buffer.length());

    EXPECT_NE(result_data.find(expected_url), std::string::npos);
    data_buffer.drain(data_buffer.length());
    EXPECT_TRUE(session_state->sessionIdFound());
    session_state->resetSessionIdFound();
  }

  // Case 5: Valid SSE response with CRLFCRLF \r\n\r\n
  {
    Envoy::Http::TestResponseHeaderMapImpl headers{{":status", "200"},
                                                   {"content-type", "text/event-stream"}};

    session_state->onUpdateHeader(host_address, headers);

    data_buffer.add(absl::StrCat("data: http://example.com?sessionId=", raw_session_id, CRLFCRLF));

    EXPECT_EQ(session_state->onUpdateData(host_address, data_buffer, false),
              Envoy::Http::FilterDataStatus::Continue);

    const std::string expected_url =
        absl::StrCat("http://example.com?sessionId=", raw_session_id, ".", encoded_host);

    const std::string result_data(
        static_cast<const char*>(data_buffer.linearize(data_buffer.length())),
        data_buffer.length());

    EXPECT_NE(result_data.find(expected_url), std::string::npos);
    data_buffer.drain(data_buffer.length());
    EXPECT_TRUE(session_state->sessionIdFound());
    session_state->resetSessionIdFound();
  }

  // Case 6: sessionId contains SEPARATOR ('.') in the middle (e.g. "abc.def.ghi")
  {
    Envoy::Http::TestResponseHeaderMapImpl headers{{":status", "200"},
                                                   {"content-type", "text/event-stream"}};

    session_state->onUpdateHeader(host_address, headers);

    const std::string raw_session_id_with_separator = "abc.def.ghi";
    data_buffer.add(
        absl::StrCat("data: http://example.com?sessionId=", raw_session_id_with_separator, LFLF));

    EXPECT_EQ(session_state->onUpdateData(host_address, data_buffer, false),
              Envoy::Http::FilterDataStatus::Continue);

    const std::string expected_url = absl::StrCat(
        "http://example.com?sessionId=", raw_session_id_with_separator, ".", encoded_host);

    const std::string result_data(
        static_cast<const char*>(data_buffer.linearize(data_buffer.length())),
        data_buffer.length());

    EXPECT_NE(result_data.find(expected_url), std::string::npos);
    data_buffer.drain(data_buffer.length());
    EXPECT_TRUE(session_state->sessionIdFound());
    session_state->resetSessionIdFound();
  }

  // Case 7: after sessionId is found, no more data should be processed
  {
    Envoy::Http::TestResponseHeaderMapImpl headers{{":status", "200"},
                                                   {"content-type", "text/event-stream"}};

    session_state->onUpdateHeader(host_address, headers);
    data_buffer.add(absl::StrCat("data: http://example.com?sessionId=", raw_session_id, LFLF));
    EXPECT_EQ(session_state->onUpdateData(host_address, data_buffer, false),
              Envoy::Http::FilterDataStatus::Continue);
    EXPECT_TRUE(session_state->sessionIdFound());
    data_buffer.drain(data_buffer.length());

    // Add more data
    data_buffer.add(absl::StrCat("data: abcdefg")); // no LFLF at the end, incomplete chunk
    EXPECT_EQ(session_state->onUpdateData(host_address, data_buffer, false),
              Envoy::Http::FilterDataStatus::Continue);

    EXPECT_NE(data_buffer.length(), 0);
  }
}
TEST(EnvelopeSessionStateFactoryTest, EnvelopeSessionStateTestSse) {
  {
    EnvelopeSessionStateProto config;
    config.set_param_name("sessionId");
    config.add_chunk_end_patterns("\r\n\r\n");
    config.add_chunk_end_patterns("\n\n");
    config.add_chunk_end_patterns("\r\r");
    EnvelopeSessionStateFactory factory(config);

    // Case 1: Path not exist
    Envoy::Http::TestRequestHeaderMapImpl request_headers1;
    auto session_state1 = factory.create(request_headers1);
    EXPECT_EQ(absl::nullopt, session_state1->upstreamAddress());

    // Case 2: Query parameter not exist
    Envoy::Http::TestRequestHeaderMapImpl request_headers2{{":path", "/path"}};
    auto session_state2 = factory.create(request_headers2);
    EXPECT_EQ(absl::nullopt, session_state2->upstreamAddress());

    // Case 3: Session value has no separator
    const std::string raw_session_id = "abcdefg";
    Envoy::Http::TestRequestHeaderMapImpl request_headers3{
        {":path", "/path?sessionId=" + raw_session_id}};
    auto session_state3 = factory.create(request_headers3);
    EXPECT_EQ(absl::nullopt, session_state3->upstreamAddress());
    EXPECT_EQ(request_headers3.getPathValue(), "/path?sessionId=abcdefg");

    // Case 4: Session value with valid separator and encoded host
    const std::string host = "1.2.3.4:80";
    const std::string encoded_host = Envoy::Base64Url::encode(host.data(), host.size());
    const std::string session_value = raw_session_id + SEPARATOR + encoded_host;

    Envoy::Http::TestRequestHeaderMapImpl request_headers4{
        {":path", "/path?sessionId=" + session_value}};
    auto session_state4 = factory.create(request_headers4);
    ASSERT_TRUE(session_state4->upstreamAddress().has_value());
    EXPECT_EQ(session_state4->upstreamAddress().value(), "1.2.3.4:80");
    EXPECT_EQ(request_headers4.getPathValue(), "/path?sessionId=abcdefg");

    // Case 5: With additional query parameters
    Envoy::Http::TestRequestHeaderMapImpl request_headers5{
        {":path", "/path?sessionId=" + session_value + "&otherParam=highklm"}};
    auto session_state5 = factory.create(request_headers5);
    ASSERT_TRUE(session_state5->upstreamAddress().has_value());
    EXPECT_EQ(session_state5->upstreamAddress().value(), "1.2.3.4:80");
    EXPECT_EQ(request_headers5.getPathValue(), "/path?sessionId=abcdefg&otherParam=highklm");
  }
}

TEST(EnvelopeSessionStateFactoryTest, EnvelopeSessionStateTestMaxPendingChunkSize) {
  EnvelopeSessionStateProto config;
  config.set_param_name("sessionId");
  config.add_chunk_end_patterns("\r\n\r\n");
  config.add_chunk_end_patterns("\n\n");
  config.add_chunk_end_patterns("\r\r");
  EnvelopeSessionStateFactory factory(config);
  Envoy::Http::TestRequestHeaderMapImpl request_headers;
  auto session_state = factory.create(request_headers);

  const std::string host_address = "1.2.3.4:80";

  Envoy::Http::TestResponseHeaderMapImpl headers{{":status", "200"},
                                                 {"content-type", "text/event-stream"}};

  session_state->onUpdateHeader(host_address, headers);

  // Base64Url encoded host address
  const std::string encoded_host =
      Envoy::Base64Url::encode(host_address.data(), host_address.size());

  Buffer::OwnedImpl data_buffer;

  // Generate data larger than 4KB and add it to data_buffer
  std::string large_data(5 * 1024, 'x');
  data_buffer.add(large_data);

  // Call onUpdateData (this will move data to pending_chunk_)
  EXPECT_EQ(session_state->onUpdateData(host_address, data_buffer, false),
            Envoy::Http::FilterDataStatus::Continue);

  // Check if the session ID is found
  EXPECT_TRUE(session_state->sessionIdFound());

  data_buffer.drain(data_buffer.length());
}

TEST(EnvelopeSessionStateFactoryTest, EnvelopeSessionStateTestEndStream) {
  EnvelopeSessionStateProto config;
  config.set_param_name("sessionId");
  config.add_chunk_end_patterns("\r\n\r\n");
  config.add_chunk_end_patterns("\n\n");
  config.add_chunk_end_patterns("\r\r");
  EnvelopeSessionStateFactory factory(config);
  Envoy::Http::TestRequestHeaderMapImpl request_headers;
  auto session_state = factory.create(request_headers);

  const std::string host_address = "1.2.3.4:80";

  Envoy::Http::TestResponseHeaderMapImpl headers{{":status", "200"},
                                                 {"content-type", "text/event-stream"}};

  session_state->onUpdateHeader(host_address, headers);

  // Base64Url encoded host address
  const std::string encoded_host =
      Envoy::Base64Url::encode(host_address.data(), host_address.size());

  Buffer::OwnedImpl data_buffer;

  // data contain no end of line at the end, incomplete chunk
  data_buffer.add("data: abcdefg");

  // Call onUpdateData
  EXPECT_EQ(session_state->onUpdateData(host_address, data_buffer, true),
            Envoy::Http::FilterDataStatus::Continue);

  // data_buffer should not be drained, because it's end of stream
  EXPECT_NE(data_buffer.length(), 0);

  data_buffer.drain(data_buffer.length());
}

TEST(EnvelopeSessionStateFactoryTest, EnvelopeSessionStateTestCustomizedChunkEndPatterns) {
  EnvelopeSessionStateProto config;
  config.set_param_name("sessionId");
  config.add_chunk_end_patterns("chunk_end_pattern1");
  config.add_chunk_end_patterns("chunk_end_pattern2");
  EnvelopeSessionStateFactory factory(config);
  Envoy::Http::TestRequestHeaderMapImpl request_headers;
  auto session_state = factory.create(request_headers);

  const std::string host_address = "1.2.3.4:80";

  Envoy::Http::TestResponseHeaderMapImpl headers{{":status", "200"},
                                                 {"content-type", "text/event-stream"}};

  session_state->onUpdateHeader(host_address, headers);

  Buffer::OwnedImpl data_buffer;

  // Case 1: data contain chunk_end_pattern1
  data_buffer.add("data: http://example.com?sessionId=abcdefg.chunk_end_pattern1");

  // Call onUpdateData
  EXPECT_EQ(session_state->onUpdateData(host_address, data_buffer, true),
            Envoy::Http::FilterDataStatus::Continue);

  // sessionId should be found
  EXPECT_TRUE(session_state->sessionIdFound());
  session_state->resetSessionIdFound();
  data_buffer.drain(data_buffer.length());

  // Case 2: data contain chunk_end_pattern2
  data_buffer.add("data: http://example.com?sessionId=abcdefg.chunk_end_pattern2");

  // Call onUpdateData
  EXPECT_EQ(session_state->onUpdateData(host_address, data_buffer, true),
            Envoy::Http::FilterDataStatus::Continue);

  // sessionId should be found
  EXPECT_TRUE(session_state->sessionIdFound());
  session_state->resetSessionIdFound();
  data_buffer.drain(data_buffer.length());

  // Case 3: data contain both chunk_end_pattern1 and chunk_end_pattern2
  data_buffer.add("data: http://example.com?sessionId=abcdefg\n\n");

  // Call onUpdateData
  EXPECT_EQ(session_state->onUpdateData(host_address, data_buffer, true),
            Envoy::Http::FilterDataStatus::Continue);

  // sessionId should not be found, cause \n\n nolonger a valid chunk end pattern
  EXPECT_FALSE(session_state->sessionIdFound());
  session_state->resetSessionIdFound();
  data_buffer.drain(data_buffer.length());
}

} // namespace
} // namespace Envelope
} // namespace SseSessionState
} // namespace Http
} // namespace Extensions
} // namespace Envoy
