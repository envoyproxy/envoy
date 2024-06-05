#pragma once

#include "envoy/config/core/v3/base.pb.h"

#include "source/common/common/empty_string.h"
#include "source/common/network/resolver_impl.h"
#include "source/common/network/socket_impl.h"
#include "source/common/network/utility.h"

#include "test/common/stream_info/test_util.h"
#include "test/fuzz/common.pb.h"
#include "test/mocks/ssl/mocks.h"
#include "test/mocks/upstream/host.h"
#include "test/test_common/utility.h"

#include "quiche/http2/adapter/header_validator.h"

// Strong assertion that applies across all compilation modes and doesn't rely
// on gtest, which only provides soft fails that don't trip oss-fuzz failures.
#define FUZZ_ASSERT(x) RELEASE_ASSERT(x, "")

namespace Envoy {
namespace Fuzz {

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

// Replace invalid host characters.
inline std::string replaceInvalidHostCharacters(absl::string_view string) {
  std::string filtered;
  filtered.reserve(string.length());
  for (const char& c : string) {
    if (http2::adapter::HeaderValidator::IsValidAuthority(absl::string_view(&c, 1))) {
      filtered.push_back(c);
    } else {
      filtered.push_back('0');
    }
  }
  return filtered;
}

inline envoy::config::core::v3::Metadata
replaceInvalidStringValues(const envoy::config::core::v3::Metadata& upstream_metadata) {
  envoy::config::core::v3::Metadata processed = upstream_metadata;
  for (auto& metadata_struct : *processed.mutable_filter_metadata()) {
    // Metadata fields consist of keyed Structs, which is a map of dynamically typed values. These
    // values can be null, a number, a string, a boolean, a list of values, or a recursive struct.
    // This clears any invalid characters in string values. It may not be likely a coverage-driven
    // fuzzer will explore recursive structs, so this case is not handled here.
    for (auto& field : *metadata_struct.second.mutable_fields()) {
      if (field.second.kind_case() == ProtobufWkt::Value::kStringValue) {
        field.second.set_string_value(replaceInvalidCharacters(field.second.string_value()));
      }
    }
  }
  return processed;
}

// Convert from test proto Headers to a variant of TestHeaderMapImpl. Validate proto if you intend
// to sanitize for invalid header characters.
template <class T>
inline T fromHeaders(
    const test::fuzz::Headers& headers,
    const absl::node_hash_set<std::string>& ignore_headers = absl::node_hash_set<std::string>(),
    absl::node_hash_set<std::string> include_headers = absl::node_hash_set<std::string>()) {
  T header_map;
  for (const auto& header : headers.headers()) {
    if (ignore_headers.find(absl::AsciiStrToLower(header.key())) == ignore_headers.end()) {
      header_map.addCopy(header.key(), header.value());
    }
    include_headers.erase(absl::AsciiStrToLower(header.key()));
  }
  // Add dummy headers for non-present headers that must be included.
  for (const auto& header : include_headers) {
    header_map.addCopy(header, "dummy");
  }
  return header_map;
}

// Convert from test proto Metadata to MetadataMap
inline Http::MetadataMapVector fromMetadata(const test::fuzz::Metadata& metadata) {
  Http::MetadataMapVector metadata_map_vector;
  if (!metadata.metadata().empty()) {
    Http::MetadataMap metadata_map;
    Http::MetadataMapPtr metadata_map_ptr = std::make_unique<Http::MetadataMap>(metadata_map);
    for (const auto& pair : metadata.metadata()) {
      metadata_map_ptr->insert(pair);
    }
    metadata_map_vector.push_back(std::move(metadata_map_ptr));
  }
  return metadata_map_vector;
}

// Convert from HeaderMap to test proto Headers.
inline test::fuzz::Headers toHeaders(const Http::HeaderMap& headers) {
  test::fuzz::Headers fuzz_headers;
  headers.iterate([&fuzz_headers](const Http::HeaderEntry& header) -> Http::HeaderMap::Iterate {
    auto* fuzz_header = fuzz_headers.add_headers();
    fuzz_header->set_key(std::string(header.key().getStringView()));
    fuzz_header->set_value(std::string(header.value().getStringView()));
    return Http::HeaderMap::Iterate::Continue;
  });
  return fuzz_headers;
}

const std::string TestSubjectPeer =
    "CN=Test Server,OU=Lyft Engineering,O=Lyft,L=San Francisco,ST=California,C=US";

inline std::unique_ptr<TestStreamInfo> fromStreamInfo(const test::fuzz::StreamInfo& stream_info,
                                                      TimeSource& time_source) {
  // Set mocks' default string return value to be an empty string.
  // TODO(asraa): Speed up this function, which is slowed because of the use of mocks.
  testing::DefaultValue<const std::string&>::Set(EMPTY_STRING);
  auto test_stream_info = std::make_unique<TestStreamInfo>(time_source);
  test_stream_info->metadata_ = stream_info.dynamic_metadata();
  // Truncate recursive filter metadata fields.
  // TODO(asraa): Resolve MessageToJsonString failure on recursive filter metadata.
  for (auto& pair : *test_stream_info->metadata_.mutable_filter_metadata()) {
    std::string value;
    pair.second.SerializeToString(&value);
    pair.second.ParseFromString(value.substr(0, 128));
  }
  // libc++ clocks don't track at nanosecond on macOS.
  const auto start_time =
      static_cast<uint64_t>(std::numeric_limits<std::chrono::nanoseconds::rep>::max()) <
              stream_info.start_time()
          ? 0
          : stream_info.start_time() / 1000;
  test_stream_info->start_time_ = SystemTime(std::chrono::microseconds(start_time));
  if (stream_info.has_response_code()) {
    test_stream_info->setResponseCode(stream_info.response_code().value());
  }
  auto upstream_host = std::make_shared<NiceMock<Upstream::MockHostDescription>>();
  auto upstream_metadata = std::make_shared<envoy::config::core::v3::Metadata>(
      replaceInvalidStringValues(stream_info.upstream_metadata()));
  ON_CALL(*upstream_host, metadata()).WillByDefault(testing::Return(upstream_metadata));
  test_stream_info->setUpstreamInfo(std::make_shared<StreamInfo::UpstreamInfoImpl>());
  test_stream_info->upstreamInfo()->setUpstreamHost(upstream_host);
  Envoy::Network::Address::InstanceConstSharedPtr address;
  if (stream_info.has_address()) {
    if (stream_info.address().address_case() ==
            envoy::config::core::v3::Address::AddressCase::kEnvoyInternalAddress &&
        stream_info.address().envoy_internal_address().address_name_specifier_case() ==
            envoy::config::core::v3::EnvoyInternalAddress::AddressNameSpecifierCase::
                kServerListenerName) {
      address = std::make_shared<Envoy::Network::Address::EnvoyInternalInstance>(
          replaceInvalidHostCharacters(
              stream_info.address().envoy_internal_address().server_listener_name()));
    } else {
      auto address_or_error = Envoy::Network::Address::resolveProtoAddress(stream_info.address());
      THROW_IF_STATUS_NOT_OK(address_or_error, throw);
      address = address_or_error.value();
    }
  } else {
    address = *Network::Utility::resolveUrl("tcp://10.0.0.1:443");
  }
  Envoy::Network::Address::InstanceConstSharedPtr upstream_local_address;
  if (stream_info.has_upstream_local_address()) {
    auto upstream_local_address_or_error =
        Envoy::Network::Address::resolveProtoAddress(stream_info.upstream_local_address());
    THROW_IF_STATUS_NOT_OK(upstream_local_address_or_error, throw);
    upstream_local_address = upstream_local_address_or_error.value();
  } else {
    upstream_local_address = *Network::Utility::resolveUrl("tcp://10.0.0.1:10000");
  }
  test_stream_info->upstreamInfo()->setUpstreamLocalAddress(upstream_local_address);
  test_stream_info->downstream_connection_info_provider_ =
      std::make_shared<Network::ConnectionInfoSetterImpl>(address, address);
  test_stream_info->downstream_connection_info_provider_->setRequestedServerName(
      stream_info.requested_server_name());
  auto connection_info = std::make_shared<NiceMock<Ssl::MockConnectionInfo>>();
  ON_CALL(*connection_info, subjectPeerCertificate())
      .WillByDefault(testing::ReturnRef(TestSubjectPeer));
  test_stream_info->downstream_connection_info_provider_->setSslConnection(connection_info);
  return test_stream_info;
}

// Parses http or proto body into chunks.
inline std::vector<std::string> parseHttpData(const test::fuzz::HttpData& data) {
  std::vector<std::string> data_chunks;

  if (data.has_http_body()) {
    data_chunks.reserve(data.http_body().data_size());
    for (const std::string& http_data : data.http_body().data()) {
      data_chunks.push_back(http_data);
    }
  } else if (data.has_proto_body()) {
    const std::string serialized = data.proto_body().message().value();
    data_chunks = absl::StrSplit(serialized, absl::ByLength(data.proto_body().chunk_size()));
  }

  return data_chunks;
}

} // namespace Fuzz
} // namespace Envoy
