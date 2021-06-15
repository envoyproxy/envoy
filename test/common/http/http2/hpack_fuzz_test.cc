// Fuzzer for HPACK encoding and decoding.
// TODO(asraa): Speed up by using raw byte input and separators rather than protobuf input.

#include <algorithm>

#include "test/common/http/http2/hpack_fuzz.pb.validate.h"
#include "test/fuzz/fuzz_runner.h"
#include "test/test_common/utility.h"

#include "absl/container/fixed_array.h"
#include "absl/strings/escaping.h"
#include "nghttp2/nghttp2.h"

namespace Envoy {
namespace Http {
namespace Http2 {
namespace {

// Dynamic Header Table Size
constexpr int kHeaderTableSize = 4096;

std::vector<nghttp2_nv> createNameValueArray(const test::fuzz::Headers& input) {
  const size_t nvlen = input.headers().size();
  std::vector<nghttp2_nv> nva(nvlen);
  int i = 0;
  for (const auto& header : input.headers()) {
    // TODO(asraa): Consider adding flags in fuzzed input.
    const uint8_t flags = 0;
    ENVOY_LOG_MISC(trace, "encoding: {} {}", absl::CEscape(header.key()),
                   absl::CEscape(header.value()));
    nva[i++] = {const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(header.key().data())),
                const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(header.value().data())),
                header.key().size(), header.value().size(), flags};
  }

  return nva;
}

Buffer::OwnedImpl encodeHeaders(nghttp2_hd_deflater* deflater,
                                const std::vector<nghttp2_nv>& input_nv) {
  // Estimate the upper bound
  const size_t buflen = nghttp2_hd_deflate_bound(deflater, input_nv.data(), input_nv.size());

  Buffer::OwnedImpl payload;
  auto reservation = payload.reserveSingleSlice(buflen);

  // Encode using nghttp2
  uint8_t* buf = reinterpret_cast<uint8_t*>(reservation.slice().mem_);
  ASSERT(input_nv.data() != nullptr);
  const ssize_t result =
      nghttp2_hd_deflate_hd(deflater, buf, buflen, input_nv.data(), input_nv.size());
  ASSERT(result >= 0, absl::StrCat("Failed to decode with result ", result));

  reservation.commit(result);

  return payload;
}

std::vector<std::pair<std::string, std::string>>
decodeHeaders(nghttp2_hd_inflater* inflater, const Buffer::OwnedImpl& payload, bool end_headers) {
  // Decode using nghttp2
  Buffer::RawSliceVector slices = payload.getRawSlices();
  const int num_slices = slices.size();
  ASSERT(num_slices == 1, absl::StrCat("number of slices ", num_slices));

  std::vector<std::pair<std::string, std::string>> decoded_headers;
  int inflate_flags = 0;
  nghttp2_nv nv;
  while (slices[0].len_ > 0) {
    ssize_t result = nghttp2_hd_inflate_hd2(inflater, &nv, &inflate_flags,
                                            reinterpret_cast<uint8_t*>(slices[0].mem_),
                                            slices[0].len_, end_headers);
    // Decoding should not fail and data should not be left in slice.
    ASSERT(result >= 0);

    slices[0].mem_ = reinterpret_cast<uint8_t*>(slices[0].mem_) + result;
    slices[0].len_ -= result;

    if (inflate_flags & NGHTTP2_HD_INFLATE_EMIT) {
      // One header key value pair has been successfully decoded.
      decoded_headers.push_back({std::string(reinterpret_cast<char*>(nv.name), nv.namelen),
                                 std::string(reinterpret_cast<char*>(nv.value), nv.valuelen)});
      ENVOY_LOG_MISC(trace, "decoded: {}, {}", absl::CEscape(decoded_headers.back().first),
                     absl::CEscape(decoded_headers.back().second));
    }
  }

  if (end_headers) {
    nghttp2_hd_inflate_end_headers(inflater);
  }

  return decoded_headers;
}

struct NvComparator {
  inline bool operator()(const nghttp2_nv& a, const nghttp2_nv& b) const {
    absl::string_view a_name(reinterpret_cast<char*>(a.name), a.namelen);
    absl::string_view b_name(reinterpret_cast<char*>(b.name), b.namelen);
    if (a_name != b_name) {
      return a_name < b_name;
    }
    absl::string_view a_val(reinterpret_cast<char*>(a.value), a.valuelen);
    absl::string_view b_val(reinterpret_cast<char*>(b.value), b.valuelen);
    return a_val < b_val;
  }
};

DEFINE_PROTO_FUZZER(const test::common::http::http2::HpackTestCase& input) {
  // Validate headers.
  try {
    TestUtility::validate(input);
  } catch (const EnvoyException& e) {
    ENVOY_LOG_MISC(trace, "EnvoyException: {}", e.what());
    return;
  }

  // Create name value pairs from headers.
  std::vector<nghttp2_nv> input_nv = createNameValueArray(input.headers());
  // Skip encoding an empty header map. nghttp2 will throw a nullptr error on runtime if it receives
  // a nullptr input.
  if (!input_nv.data()) {
    return;
  }

  // Create Deflater and Inflater
  nghttp2_hd_deflater* deflater = nullptr;
  int rc = nghttp2_hd_deflate_new(&deflater, kHeaderTableSize);
  ASSERT(rc == 0);
  nghttp2_hd_inflater* inflater = nullptr;
  rc = nghttp2_hd_inflate_new(&inflater);
  ASSERT(rc == 0);

  // Encode headers with nghttp2.
  const Buffer::OwnedImpl payload = encodeHeaders(deflater, input_nv);
  ASSERT(!payload.getRawSlices().empty());

  // Decode headers with nghttp2
  std::vector<std::pair<std::string, std::string>> output_nv =
      decodeHeaders(inflater, payload, input.end_headers());

  // Verify that decoded == encoded.
  ASSERT(input_nv.size() == output_nv.size());
  std::sort(input_nv.begin(), input_nv.end(), NvComparator());
  std::sort(output_nv.begin(), output_nv.end());

  for (size_t i = 0; i < input_nv.size(); i++) {
    std::string in_name = {reinterpret_cast<char*>(input_nv[i].name), input_nv[i].namelen};
    std::string out_name = output_nv[i].first;
    std::string in_val = {reinterpret_cast<char*>(input_nv[i].value), input_nv[i].valuelen};
    std::string out_val = output_nv[i].second;
    ASSERT(in_name == out_name);
    ASSERT(in_val == out_val);
  }

  // Delete inflater
  nghttp2_hd_inflate_del(inflater);
  // Delete deflater.
  nghttp2_hd_deflate_del(deflater);
}

} // namespace
} // namespace Http2
} // namespace Http
} // namespace Envoy
