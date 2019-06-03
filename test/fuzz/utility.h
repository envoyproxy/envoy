#pragma once

#include "common/network/utility.h"

#include "test/common/stream_info/test_util.h"
#include "test/fuzz/common.pb.h"
#include "test/mocks/upstream/host.h"
#include "test/test_common/utility.h"

namespace Envoy {
namespace Fuzz {

// Convert from test proto Headers to TestHeaderMapImpl.
inline Http::TestHeaderMapImpl fromHeaders(
    const test::fuzz::Headers& headers,
    const std::unordered_set<std::string>& ignore_headers = std::unordered_set<std::string>()) {
  Http::TestHeaderMapImpl header_map;
  for (const auto& header : headers.headers()) {
    // HeaderMapImpl and places such as the route lookup should never see strings with embedded NULL
    // values, the HTTP codecs should reject them. So, don't inject any such strings into the fuzz
    // tests.
    const auto clean = [](const std::string& s) {
      const auto n = s.find('\0');
      if (n == std::string::npos) {
        return s;
      }
      return s.substr(0, n);
    };
    // When we are injecting headers, we don't allow the key to ever be empty,
    // since calling code is not supposed to do this.
    const std::string key = header.key().empty() ? "not-empty" : clean(header.key());
    if (ignore_headers.find(StringUtil::toLower(key)) != ignore_headers.end()) {
      header_map.addCopy(key, clean(header.value()));
    }
  }
  return header_map;
}

// Convert from HeaderMap to test proto Headers.
inline test::fuzz::Headers toHeaders(const Http::HeaderMap& headers) {
  test::fuzz::Headers fuzz_headers;
  headers.iterate(
      [](const Http::HeaderEntry& header, void* ctxt) -> Http::HeaderMap::Iterate {
        auto* fuzz_header = static_cast<test::fuzz::Headers*>(ctxt)->add_headers();
        fuzz_header->set_key(std::string(header.key().getStringView()));
        fuzz_header->set_value(std::string(header.value().getStringView()));
        return Http::HeaderMap::Iterate::Continue;
      },
      &fuzz_headers);
  return fuzz_headers;
}

inline TestStreamInfo fromStreamInfo(const test::fuzz::StreamInfo& stream_info) {
  TestStreamInfo test_stream_info;
  test_stream_info.metadata_ = stream_info.dynamic_metadata();
  // libc++ clocks don't track at nanosecond on macOS.
  const auto start_time =
      static_cast<uint64_t>(std::numeric_limits<std::chrono::nanoseconds::rep>::max()) <
              stream_info.start_time()
          ? 0
          : stream_info.start_time() / 1000;
  test_stream_info.start_time_ = SystemTime(std::chrono::microseconds(start_time));
  if (stream_info.has_response_code()) {
    test_stream_info.response_code_ = stream_info.response_code().value();
  }
  auto upstream_host = std::make_shared<NiceMock<Upstream::MockHostDescription>>();
  auto upstream_metadata =
      std::make_shared<envoy::api::v2::core::Metadata>(stream_info.upstream_metadata());
  ON_CALL(*upstream_host, metadata()).WillByDefault(testing::Return(upstream_metadata));
  test_stream_info.upstream_host_ = upstream_host;
  auto address = Network::Utility::resolveUrl("tcp://10.0.0.1:443");
  test_stream_info.upstream_local_address_ = address;
  test_stream_info.downstream_local_address_ = address;
  test_stream_info.downstream_direct_remote_address_ = address;
  test_stream_info.downstream_remote_address_ = address;
  return test_stream_info;
}

// The HeaderMap code assumes that input does not contain certain characters, and
// this is validated by the HTTP parser. Some fuzzers will create strings with
// these characters, however, and this creates not very interesting fuzz test
// failures as an assertion is rapidly hit in the LowerCaseString constructor
// before we get to anything interesting.
//
// This method will replace any of those characters found with spaces.
inline std::string replaceInvalidCharacters(absl::string_view string) {
  std::string filtered;
  filtered.reserve(string.length());
  for (const char& c : string) {
    switch (c) {
    case '\0':
      FALLTHRU;
    case '\r':
      FALLTHRU;
    case '\n':
      filtered.push_back(' ');
      break;
    default:
      filtered.push_back(c);
    }
  }
  return filtered;
}

// Return a new RepeatedPtrField of HeaderValueOptions with invalid characters removed.
inline Protobuf::RepeatedPtrField<envoy::api::v2::core::HeaderValueOption> replaceInvalidHeaders(
    const Protobuf::RepeatedPtrField<envoy::api::v2::core::HeaderValueOption>& headers_to_add) {
  Protobuf::RepeatedPtrField<envoy::api::v2::core::HeaderValueOption> processed;
  for (const auto& header : headers_to_add) {
    auto* header_value_option = processed.Add();
    auto* mutable_header = header_value_option->mutable_header();
    mutable_header->set_key(replaceInvalidCharacters(header.header().key()));
    mutable_header->set_value(replaceInvalidCharacters(header.header().value()));
    header_value_option->mutable_append()->CopyFrom(header.append());
  }
  return processed;
}

} // namespace Fuzz
} // namespace Envoy
