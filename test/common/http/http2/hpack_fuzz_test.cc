// Fuzzer for HPACK encoding and decoding.
// TODO(asraa): Speed up by using raw byte input and separators rather than protobuf input.

#include <algorithm>

#include "test/common/http/http2/hpack_fuzz.pb.validate.h"
#include "test/fuzz/fuzz_runner.h"
#include "test/test_common/utility.h"

#include "absl/container/fixed_array.h"
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
    uint8_t flags = 0;
    std::string key = LowerCaseString(header.key()).get();
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

  Buffer::RawSlice iovec;
  Buffer::OwnedImpl payload;
  payload.reserve(buflen, &iovec, 1);
  ASSERT(iovec.len_ >= buflen);

  // Encode using nghttp2
  uint8_t* buf = reinterpret_cast<uint8_t*>(iovec.mem_);
  ASSERT(input_nv.data() != nullptr);
  const ssize_t result =
      nghttp2_hd_deflate_hd(deflater, buf, buflen, input_nv.data(), input_nv.size());
  ASSERT(result >= 0, absl::StrCat("Failed to decode with result ", result));

  iovec.len_ = result;
  payload.commit(&iovec, 1);

  return payload;
}

std::vector<nghttp2_nv> decodeHeaders(nghttp2_hd_inflater* inflater,
                                      const Buffer::OwnedImpl& payload, bool end_headers) {
  // Decode using nghttp2
  Buffer::RawSliceVector slices = payload.getRawSlices();
  const int num_slices = slices.size();
  ASSERT(num_slices == 1, absl::StrCat("number of slices ", num_slices));

  std::vector<nghttp2_nv> decoded_headers;
  int inflate_flags = 0;
  nghttp2_nv decoded_nv;
  while (slices[0].len_ > 0) {
    ssize_t result = nghttp2_hd_inflate_hd2(inflater, &decoded_nv, &inflate_flags,
                                            reinterpret_cast<uint8_t*>(slices[0].mem_),
                                            slices[0].len_, end_headers);
    // Decoding should not fail and data should not be left in slice.
    ASSERT(result >= 0);

    slices[0].mem_ = reinterpret_cast<void*>(reinterpret_cast<uint8_t*>(slices[0].mem_) + result);
    slices[0].len_ -= result;

    if (inflate_flags & NGHTTP2_HD_INFLATE_EMIT) {
      // One header key value pair has been successfully decoded.
      decoded_headers.push_back(decoded_nv);
    }
  }

  if (end_headers) {
    nghttp2_hd_inflate_end_headers(inflater);
  }

  return decoded_headers;
}

int nvCompare(const void* a_in, const void* b_in) {
  const nghttp2_nv* a = reinterpret_cast<const nghttp2_nv*>(a_in);
  const nghttp2_nv* b = reinterpret_cast<const nghttp2_nv*>(b_in);

  absl::string_view a_str(reinterpret_cast<char*>(a->name), a->namelen);
  absl::string_view b_str(reinterpret_cast<char*>(b->name), b->namelen);
  if (a_str > b_str) {
    return 1;
  }
  if (a_str < b_str) {
    return -1;
  }
  return 0;
}

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
  // Skip encoding empty headers. nghttp2 will throw a nullptr error on runtime if it receives a
  // nullptr input.
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
  std::vector<nghttp2_nv> output_nv = decodeHeaders(inflater, payload, input.end_headers());

  // Verify that decoded == encoded.
  ASSERT(input_nv.size() == output_nv.size());
  std::qsort(input_nv.data(), input_nv.size(), sizeof(nghttp2_nv), nvCompare);
  std::qsort(output_nv.data(), output_nv.size(), sizeof(nghttp2_nv), nvCompare);
  for (size_t i = 0; i < input_nv.size(); i++) {
    absl::string_view in_name = {reinterpret_cast<char*>(input_nv[i].name), input_nv[i].namelen};
    absl::string_view out_name = {reinterpret_cast<char*>(output_nv[i].name), output_nv[i].namelen};
    absl::string_view in_val = {reinterpret_cast<char*>(input_nv[i].value), input_nv[i].valuelen};
    absl::string_view out_val = {reinterpret_cast<char*>(output_nv[i].value),
                                 output_nv[i].valuelen};
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
