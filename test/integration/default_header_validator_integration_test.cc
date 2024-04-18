#include "source/common/http/character_set_validation.h"
#include "source/extensions/http/header_validators/envoy_default/character_tables.h"

#include "test/integration/http_protocol_integration.h"

namespace Envoy {

class DownstreamUhvIntegrationTest : public HttpProtocolIntegrationTest {
public:
  using HttpProtocolIntegrationTest::HttpProtocolIntegrationTest;

  // This method sends requests with the :path header formatted using `path_format` where
  // one character is replaced with a value from [0 to 0xFF].
  // If the replacement value is in either `uhv_allowed_characters` or
  // `additionally_allowed_characters` sets then the request is expected to succeed and the :path
  // sent by Envoy is checked against the value produced by the `expected_path_builder` functor.
  // This allows validation of path normalization, fragment stripping, etc. If the replacement value
  // is not in either set, then the request is expected to be rejected.
  using PathFormatter = std::function<std::string(char)>;
  void
  validateCharacterSetInUrl(PathFormatter path_formatter,
                            const std::array<uint32_t, 8>& uhv_allowed_characters,
                            absl::string_view additionally_allowed_characters,
                            const std::function<std::string(uint32_t)>& expected_path_builder) {
    std::vector<FakeStreamPtr> upstream_requests;
    for (uint32_t ascii = 0x0; ascii <= 0xFF; ++ascii) {
      // Skip cases where test client can not produce a request
      if ((downstream_protocol_ == Http::CodecType::HTTP3 ||
           (downstream_protocol_ == Http::CodecType::HTTP2 &&
            GetParam().http2_implementation == Http2Impl::Oghttp2)) &&
          ascii == 0) {
        // QUIC client does weird things when a header contains nul character
        // oghttp2 replaces 0 with , in the URL path
        continue;
      } else if (downstream_protocol_ == Http::CodecType::HTTP1 &&
                 (ascii == '\r' || ascii == '\n')) {
        // \r and \n will produce invalid HTTP/1 request on the wire
        continue;
      }
      auto client = makeHttpConnection(lookupPort("http"));

      std::string path = path_formatter(static_cast<char>(ascii));
      Http::HeaderString invalid_value{};
      invalid_value.setCopyUnvalidatedForTestOnly(path);
      Http::TestRequestHeaderMapImpl headers{
          {":scheme", "https"}, {":authority", "envoy.com"}, {":method", "GET"}};
      headers.addViaMove(Http::HeaderString(absl::string_view(":path")), std::move(invalid_value));
      auto response = client->makeHeaderOnlyRequest(headers);

      if (Http::testCharInTable(uhv_allowed_characters, static_cast<char>(ascii)) ||
          absl::StrContains(additionally_allowed_characters, static_cast<char>(ascii))) {
        waitForNextUpstreamRequest();
        std::string expected_path = expected_path_builder(ascii);
        EXPECT_EQ(upstream_request_->headers().getPathValue(), expected_path);
        // Send a headers only response.
        upstream_request_->encodeHeaders(default_response_headers_, true);
        ASSERT_TRUE(response->waitForEndStream());
        upstream_requests.emplace_back(std::move(upstream_request_));
      } else {
        ASSERT_TRUE(client->waitForDisconnect());
        if (downstream_protocol_ == Http::CodecType::HTTP1) {
          EXPECT_EQ("400", response->headers().getStatusValue());
        } else {
          EXPECT_TRUE(response->reset());
        }
      }
      client->close();
    }
  }

  void enableOghttp2ForFakeUpstream() {
    // Enable most permissive codec for fake upstreams, so it can accept unencoded TAB and space
    // from the H/3 downstream
    envoy::config::core::v3::Http2ProtocolOptions config;
    config.mutable_use_oghttp2_codec()->set_value(true);
    mergeOptions(config);
  }

  std::string generateExtendedAsciiString() {
    std::string extended_ascii_string;
    for (uint32_t ascii = 0x80; ascii <= 0xff; ++ascii) {
      extended_ascii_string.push_back(static_cast<char>(ascii));
    }
    return extended_ascii_string;
  }

  std::string additionallyAllowedCharactersInUrlPath() {
    // All codecs allow the following characters that are outside of RFC "<>[]^`{}\|
    std::string additionally_allowed_characters(R"--("<>[]^`{}\|)--");
    if (downstream_protocol_ == Http::CodecType::HTTP3) {
      // In addition H/3 allows TAB and SPACE in path
      additionally_allowed_characters += +"\t ";
    } else if (downstream_protocol_ == Http::CodecType::HTTP2) {
      // Both nghttp2 and oghttp2 allow extended ASCII >= 0x80 in path
      additionally_allowed_characters += generateExtendedAsciiString();
      if (GetParam().http2_implementation == Http2Impl::Oghttp2) {
        // In addition H/2 oghttp2 allows TAB and SPACE in path
        additionally_allowed_characters += +"\t ";
      }
    }
    return additionally_allowed_characters;
  }

  void setupCharacterValidationRuntimeValues() {
    // This allows sending NUL, CR and LF in headers without triggering ASSERTs in Envoy
    Http::HeaderStringValidator::disable_validation_for_tests_ = true;
    disable_client_header_validation_ = true;
    config_helper_.addRuntimeOverride("envoy.reloadable_features.validate_upstream_headers",
                                      "false");
    config_helper_.addRuntimeOverride("envoy.reloadable_features.http_reject_path_with_fragment",
                                      "false");
  }
};

INSTANTIATE_TEST_SUITE_P(Protocols, DownstreamUhvIntegrationTest,
                         testing::ValuesIn(HttpProtocolIntegrationTest::getProtocolTestParams(
                             {Http::CodecType::HTTP1, Http::CodecType::HTTP2,
                              Http::CodecType::HTTP3},
                             {Http::CodecType::HTTP2})),
                         HttpProtocolIntegrationTest::protocolTestParamsToString);

// Without the `allow_non_compliant_characters_in_path` override UHV rejects requests with backslash
// in the path.
TEST_P(DownstreamUhvIntegrationTest, BackslashInUriPathConversionWithUhvOverride) {
  config_helper_.addRuntimeOverride("envoy.uhv.allow_non_compliant_characters_in_path", "false");
  disable_client_header_validation_ = true;
  config_helper_.addConfigModifier(
      [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
             hcm) -> void { hcm.mutable_normalize_path()->set_value(true); });
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  // Start the request.
  auto response = codec_client_->makeHeaderOnlyRequest(
      Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                     {":path", "/path\\with%5Cback%5Cslashes"},
                                     {":scheme", "http"},
                                     {":authority", "host"}});
  if (use_universal_header_validator_) {
    // By default Envoy disconnects connection on protocol errors
    ASSERT_TRUE(codec_client_->waitForDisconnect());
    if (downstream_protocol_ != Http::CodecType::HTTP2) {
      ASSERT_TRUE(response->complete());
      EXPECT_EQ("400", response->headers().getStatusValue());
    } else {
      ASSERT_TRUE(response->reset());
      EXPECT_EQ(Http::StreamResetReason::ConnectionTermination, response->resetReason());
    }
  } else {
    waitForNextUpstreamRequest();

    EXPECT_EQ(upstream_request_->headers().getPathValue(), "/path/with%5Cback%5Cslashes");

    // Send a headers only response.
    upstream_request_->encodeHeaders(default_response_headers_, true);
    ASSERT_TRUE(response->waitForEndStream());
  }
}

// By default the `allow_non_compliant_characters_in_path` == true and UHV behaves just like legacy
// path normalization.
TEST_P(DownstreamUhvIntegrationTest, BackslashInUriPathConversion) {
  config_helper_.addConfigModifier(
      [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
             hcm) -> void { hcm.mutable_normalize_path()->set_value(true); });
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  // Start the request.
  auto response = codec_client_->makeHeaderOnlyRequest(
      Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                     {":path", "/path\\with%5Cback%5Cslashes"},
                                     {":scheme", "http"},
                                     {":authority", "host"}});
  waitForNextUpstreamRequest();

  EXPECT_EQ(upstream_request_->headers().getPathValue(), "/path/with%5Cback%5Cslashes");

  // Send a headers only response.
  upstream_request_->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(response->waitForEndStream());
}

// By default the `uhv_preserve_url_encoded_case` == true and UHV behaves just like legacy path
// normalization.
TEST_P(DownstreamUhvIntegrationTest, UrlEncodedTripletsCasePreserved) {
  config_helper_.addConfigModifier(
      [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
             hcm) -> void { hcm.mutable_normalize_path()->set_value(true); });
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  // Start the request.
  auto response = codec_client_->makeHeaderOnlyRequest(
      Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                     {":path", "/path/with%3bmixed%5Ccase%Fesequences"},
                                     {":scheme", "http"},
                                     {":authority", "host"}});
  waitForNextUpstreamRequest();

  EXPECT_EQ(upstream_request_->headers().getPathValue(), "/path/with%3bmixed%5Ccase%Fesequences");

  // Send a headers only response.
  upstream_request_->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(response->waitForEndStream());
}

// Without the `uhv_preserve_url_encoded_case` override UHV changes all percent encoded
// sequences to use uppercase characters.
TEST_P(DownstreamUhvIntegrationTest, UrlEncodedTripletsCasePreservedWithUhvOverride) {
  config_helper_.addRuntimeOverride("envoy.uhv.preserve_url_encoded_case", "false");
  config_helper_.addConfigModifier(
      [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
             hcm) -> void { hcm.mutable_normalize_path()->set_value(true); });
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  // Start the request.
  auto response = codec_client_->makeHeaderOnlyRequest(
      Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                     {":path", "/path/with%3bmixed%5Ccase%Fesequences"},
                                     {":scheme", "http"},
                                     {":authority", "host"}});
  waitForNextUpstreamRequest();

  if (use_universal_header_validator_) {
    EXPECT_EQ(upstream_request_->headers().getPathValue(), "/path/with%3Bmixed%5Ccase%FEsequences");
  } else {
    EXPECT_EQ(upstream_request_->headers().getPathValue(), "/path/with%3bmixed%5Ccase%Fesequences");
  }
  // Send a headers only response.
  upstream_request_->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(response->waitForEndStream());
}

namespace {
std::map<char, std::string> generateExtendedAsciiPercentEncoding() {
  std::map<char, std::string> encoding;
  for (uint32_t ascii = 0x80; ascii <= 0xff; ++ascii) {
    encoding.insert(
        {static_cast<char>(ascii), fmt::format("%{:02X}", static_cast<unsigned char>(ascii))});
  }
  return encoding;
}
} // namespace

// This test shows validation of character sets in URL path for all codecs.
// It also shows that UHV in compatibility mode has the same validation.
TEST_P(DownstreamUhvIntegrationTest, CharacterValidationInPathWithoutPathNormalization) {
#ifdef WIN32
  // H/3 test on Windows is flaky
  if (downstream_protocol_ == Http::CodecType::HTTP3) {
    return;
  }
#endif
  setupCharacterValidationRuntimeValues();
  enableOghttp2ForFakeUpstream();
  initialize();
  std::string additionally_allowed_characters = additionallyAllowedCharactersInUrlPath();
  // # and ? will just cause path to be interpreted as having a query or a fragment
  // Note that the fragment will be stripped from the URL path
  additionally_allowed_characters += "?#";

  // Fragment will be stripped from path in this test
  PathFormatter path_formatter = [](char c) {
    return fmt::format("/path/with/ad{:c}itional/characters", c);
  };
  validateCharacterSetInUrl(
      path_formatter, Extensions::Http::HeaderValidators::EnvoyDefault::kPathHeaderCharTable,
      additionally_allowed_characters, [](uint32_t ascii) -> std::string {
        return ascii == '#'
                   ? "/path/with/ad"
                   : fmt::format("/path/with/ad{:c}itional/characters", static_cast<char>(ascii));
      });
}

TEST_P(DownstreamUhvIntegrationTest, CharacterValidationInPathWithPathNormalization) {
#ifdef WIN32
  // H/3 test on Windows is flaky
  if (downstream_protocol_ == Http::CodecType::HTTP3) {
    return;
  }
#endif
  setupCharacterValidationRuntimeValues();
  config_helper_.addConfigModifier(
      [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
             hcm) -> void { hcm.mutable_normalize_path()->set_value(true); });
  initialize();
  std::string additionally_allowed_characters = additionallyAllowedCharactersInUrlPath();
  // # and ? will just cause path to be interpreted as having a query or a fragment
  // Note that the fragment will be stripped from the URL path
  additionally_allowed_characters += "?#";

  std::map<char, std::string> encoded_characters{
      {'\t', "%09"}, {' ', "%20"}, {'"', "%22"}, {'<', "%3C"}, {'>', "%3E"}, {'\\', "/"},
      {'^', "%5E"},  {'`', "%60"}, {'{', "%7B"}, {'|', "%7C"}, {'}', "%7D"}};
  std::map<char, std::string> percent_encoded_extended_ascii =
      generateExtendedAsciiPercentEncoding();
  encoded_characters.merge(percent_encoded_extended_ascii);

  PathFormatter path_formatter = [](char c) {
    return fmt::format("/path/with/ad{:c}itional/characters", c);
  };
  validateCharacterSetInUrl(
      path_formatter, Http::kUriQueryAndFragmentCharTable, additionally_allowed_characters,
      [&encoded_characters](uint32_t ascii) -> std::string {
        if (ascii == '#') {
          return "/path/with/ad";
        }

        auto encoding = encoded_characters.find(static_cast<char>(ascii));
        if (encoding != encoded_characters.end()) {
          return absl::StrCat("/path/with/ad", encoding->second, "itional/characters");
        }

        return fmt::format("/path/with/ad{:c}itional/characters", static_cast<char>(ascii));
      });
}

TEST_P(DownstreamUhvIntegrationTest, CharacterValidationInQuery) {
#ifdef WIN32
  // H/3 test on Windows is flaky
  if (downstream_protocol_ == Http::CodecType::HTTP3) {
    return;
  }
#endif
  setupCharacterValidationRuntimeValues();
  // Path normalization should not affect query, however enable it to make sure it is so.
  config_helper_.addConfigModifier(
      [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
             hcm) -> void { hcm.mutable_normalize_path()->set_value(true); });
  enableOghttp2ForFakeUpstream();
  initialize();
  std::string additionally_allowed_characters = additionallyAllowedCharactersInUrlPath();
  // Adding fragment separator, since it will just cause the URL to be interpreted as having a
  // fragment Note that the fragment will be stripped from the URL path
  additionally_allowed_characters += '#';

  PathFormatter path_formatter = [](char c) {
    return fmt::format("/query?with=a{:c}ditional&characters", c);
  };
  validateCharacterSetInUrl(path_formatter, Http::kUriQueryAndFragmentCharTable,
                            additionally_allowed_characters, [](uint32_t ascii) -> std::string {
                              return ascii == '#'
                                         ? "/query?with=a"
                                         : fmt::format("/query?with=a{:c}ditional&characters",
                                                       static_cast<char>(ascii));
                            });
}

TEST_P(DownstreamUhvIntegrationTest, CharacterValidationInFragment) {
#ifdef WIN32
  // H/3 test on Windows is flaky
  if (downstream_protocol_ == Http::CodecType::HTTP3) {
    return;
  }
#endif
  setupCharacterValidationRuntimeValues();
  initialize();
  std::string additionally_allowed_characters = additionallyAllowedCharactersInUrlPath();
  // In addition all codecs allow # in fragment
  additionally_allowed_characters += '#';

  // Note that fragment is stripped from the URL path in this test
  PathFormatter path_formatter = [](char c) {
    return fmt::format("/query?with=a#frag{:c}ment", c);
  };
  validateCharacterSetInUrl(path_formatter, Http::kUriQueryAndFragmentCharTable,
                            additionally_allowed_characters,
                            [](uint32_t) -> std::string { return "/query?with=a"; });
}

// Without the `uhv_allow_malformed_url_encoding` override UHV rejects requests with malformed
// percent encoding.
TEST_P(DownstreamUhvIntegrationTest, MalformedUrlEncodedTripletsRejectedWithUhvOverride) {
  config_helper_.addRuntimeOverride("envoy.reloadable_features.uhv_allow_malformed_url_encoding",
                                    "false");
  config_helper_.addConfigModifier(
      [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
             hcm) -> void { hcm.mutable_normalize_path()->set_value(true); });
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  // Start the request.
  auto response = codec_client_->makeHeaderOnlyRequest(
      Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                     {":path", "/path%Z%30with%XYbad%7Jencoding%A"},
                                     {":scheme", "http"},
                                     {":authority", "host"}});
  if (use_universal_header_validator_) {
    // By default Envoy disconnects connection on protocol errors
    ASSERT_TRUE(codec_client_->waitForDisconnect());
    if (downstream_protocol_ != Http::CodecType::HTTP2) {
      ASSERT_TRUE(response->complete());
      EXPECT_EQ("400", response->headers().getStatusValue());
    } else {
      ASSERT_TRUE(response->reset());
      EXPECT_EQ(Http::StreamResetReason::ConnectionTermination, response->resetReason());
    }
  } else {
    waitForNextUpstreamRequest();

    EXPECT_EQ(upstream_request_->headers().getPathValue(), "/path%Z0with%XYbad%7Jencoding%A");

    // Send a headers only response.
    upstream_request_->encodeHeaders(default_response_headers_, true);
    ASSERT_TRUE(response->waitForEndStream());
  }
}

// By default the `uhv_allow_malformed_url_encoding` == true and UHV behaves just like legacy path
// normalization.
TEST_P(DownstreamUhvIntegrationTest, MalformedUrlEncodedTripletsAllowed) {
  config_helper_.addConfigModifier(
      [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
             hcm) -> void { hcm.mutable_normalize_path()->set_value(true); });
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  // Start the request.
  auto response = codec_client_->makeHeaderOnlyRequest(
      Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                     {":path", "/path%Z%30with%XYbad%7Jencoding%"},
                                     {":scheme", "http"},
                                     {":authority", "host"}});
  waitForNextUpstreamRequest();

  EXPECT_EQ(upstream_request_->headers().getPathValue(), "/path%Z0with%XYbad%7Jencoding%");

  // Send a headers only response.
  upstream_request_->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(response->waitForEndStream());
}

// Without the `envoy.uhv.reject_percent_00` override UHV rejects requests with the %00
// sequence.
TEST_P(DownstreamUhvIntegrationTest, RejectPercent00) {
  config_helper_.addConfigModifier(
      [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
             hcm) -> void { hcm.mutable_normalize_path()->set_value(true); });
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  // Start the request.
  auto response = codec_client_->makeHeaderOnlyRequest(
      Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                     {":path", "/path%00/to/something"},
                                     {":scheme", "http"},
                                     {":authority", "host"}});

  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("400", response->headers().getStatusValue());
}

TEST_P(DownstreamUhvIntegrationTest, UhvAllowsPercent00WithOverride) {
  config_helper_.addRuntimeOverride("envoy.uhv.reject_percent_00", "false");
  config_helper_.addConfigModifier(
      [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
             hcm) -> void { hcm.mutable_normalize_path()->set_value(true); });
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  // Start the request.
  auto response = codec_client_->makeHeaderOnlyRequest(
      Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                     {":path", "/path%00/to/something"},
                                     {":scheme", "http"},
                                     {":authority", "host"}});
  if (use_universal_header_validator_) {
    waitForNextUpstreamRequest();

    EXPECT_EQ(upstream_request_->headers().getPathValue(), "/path%00/to/something");

    // Send a headers only response.
    upstream_request_->encodeHeaders(default_response_headers_, true);
    ASSERT_TRUE(response->waitForEndStream());
  } else {
    // In legacy mode %00 in URL path always causes request to be rejected
    ASSERT_TRUE(response->waitForEndStream());
    ASSERT_TRUE(response->complete());
    EXPECT_EQ("400", response->headers().getStatusValue());
  }
}

} // namespace Envoy
