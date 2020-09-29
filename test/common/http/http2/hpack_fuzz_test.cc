// Fuzzer for HPACK encoding and decoding.

#include "test/common/http/http2/hpack_fuzz.pb.validate.h"
#include "test/fuzz/fuzz_runner.h"
#include "test/fuzz/utility.h"
#include "test/test_common/utility.h"

#include "absl/container/fixed_array.h"
#include "nghttp2/nghttp2.h"

namespace Envoy {
namespace Http {
namespace Http2 {
namespace {

// Dynamic Header Table Size
constexpr int kHeaderTableSize = 4096;

absl::FixedArray<nghttp2_nv> createNameValueArray(const TestRequestHeaderMapImpl& input) {
  const size_t nvlen = input.size();
  absl::FixedArray<nghttp2_nv> nva(nvlen);
  int i = 0;
  input.iterate([&nva, &i](const HeaderEntry& header) -> HeaderMap::Iterate {
    // TODO(asraa): Consider adding flags in fuzzed input.
    uint8_t flags = 0;
    nva[i] = {
        const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(header.key().getStringView().data())),
        const_cast<uint8_t*>(
            reinterpret_cast<const uint8_t*>(header.value().getStringView().data())),
        header.key().size(), header.value().size(), flags};
    i++;
    return HeaderMap::Iterate::Continue;
  });

  return nva;
}

absl::optional<Buffer::OwnedImpl> encodeHeaders(const absl::FixedArray<nghttp2_nv>& input_nv) {
  // Create Deflater
  nghttp2_hd_deflater* deflater;
  const int rv = nghttp2_hd_deflate_new(&deflater, kHeaderTableSize);
  ASSERT(rv == 0);

  // Estimate the upper bound
  const size_t buflen = nghttp2_hd_deflate_bound(deflater, input_nv.begin(), input_nv.size());

  Buffer::RawSlice iovec;
  Buffer::OwnedImpl payload;
  payload.reserve(buflen, &iovec, 1);
  ASSERT(iovec.len_ >= buflen);

  // Encode using nghttp2
  uint8_t* buf = reinterpret_cast<uint8_t*>(iovec.mem_);
  const ssize_t result =
      nghttp2_hd_deflate_hd(deflater, buf, buflen, input_nv.begin(), input_nv.size());
  if (result < 0) {
    ENVOY_LOG_MISC(trace, "Failed to decode with result {}", result);
    nghttp2_hd_deflate_del(deflater);
    return absl::nullopt;
  }

  iovec.len_ = result;
  payload.commit(&iovec, 1);

  // Delete deflater.
  nghttp2_hd_deflate_del(deflater);

  return payload;
}

TestRequestHeaderMapImpl decodeHeaders(const Buffer::OwnedImpl& payload, bool end_headers) {
  // Create inflater
  nghttp2_hd_inflater* inflater;
  const int rv = nghttp2_hd_inflate_new(&inflater);
  ASSERT(rv == 0);

  // Decode using nghttp2
  Buffer::RawSliceVector slices = payload.getRawSlices();
  const int num_slices = slices.size();
  ASSERT(num_slices == 1, absl::StrCat("number of slices ", num_slices));

  nghttp2_nv decoded_nv;
  TestRequestHeaderMapImpl decoded_headers;
  int inflate_flags = 0;
  while (slices[0].len_ > 0) {
    ssize_t result = nghttp2_hd_inflate_hd2(inflater, &decoded_nv, &inflate_flags,
                                            reinterpret_cast<uint8_t*>(slices[0].mem_),
                                            slices[0].len_, end_headers);
    // Decoding should not fail and data should not be left in slice.
    ASSERT(result >= 0 && (slices[0].len_ = 0));

    slices[0].mem_ = reinterpret_cast<void*>(reinterpret_cast<uint8_t*>(slices[0].mem_) + result);
    slices[0].len_ -= result;

    if (inflate_flags & NGHTTP2_HD_INFLATE_EMIT) {
      // One header key value pair has been successfully decoded.
      decoded_headers.addCopy(
          std::string(reinterpret_cast<char*>(decoded_nv.name), decoded_nv.namelen),
          std::string(reinterpret_cast<char*>(decoded_nv.value), decoded_nv.valuelen));
    }
  }

  if (end_headers) {
    nghttp2_hd_inflate_end_headers(inflater);
  }

  // Delete inflater
  nghttp2_hd_inflate_del(inflater);

  return decoded_headers;
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
  const TestRequestHeaderMapImpl headers =
      Fuzz::fromHeaders<TestRequestHeaderMapImpl>(input.headers());
  const absl::FixedArray<nghttp2_nv> input_nv = createNameValueArray(headers);

  // Encode headers with nghttp2.
  ENVOY_LOG_MISC(trace, "Encoding headers {}", headers);
  const absl::optional<Buffer::OwnedImpl> payload = encodeHeaders(input_nv);
  if (!payload.has_value() || payload.value().getRawSlices().size() == 0) {
    // An empty header map produces no payload, skip decoding.
    return;
  }

  // Decode headers with nghttp2
  const TestRequestHeaderMapImpl decoded_headers =
      decodeHeaders(payload.value(), input.end_headers());
  ENVOY_LOG_MISC(trace, "Decoded headers {}", decoded_headers);

  // Verify that decoded == encoded.
  FUZZ_ASSERT(headers == decoded_headers);
}

} // namespace
} // namespace Http2
} // namespace Http
} // namespace Envoy
