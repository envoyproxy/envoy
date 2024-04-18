#include "source/common/buffer/buffer_impl.h"
#include "source/common/grpc/codec.h"

#include "test/fuzz/fuzz_runner.h"
#include "test/fuzz/utility.h"
#include "test/proto/helloworld.pb.h"

namespace Envoy {
namespace Grpc {
namespace Fuzz {

// Fuzz the Grpc::decode() implementation, validating that decode(encode(x)) ==
// x for all x, regardless of how the encoded buffer is partitioned. Models
// frame boundary conditions and also trailing random crud, which effectively
// models line noise input to the decoder as well.
DEFINE_FUZZER(const uint8_t* buf, size_t len) {
  FuzzedDataProvider provider(buf, len);

  // We probably won't learn a ton more after a few frames worth.
  const uint32_t num_encode_frames = provider.ConsumeIntegralInRange(0, 3);
  // Model a buffer containing the wire input to the decoder.
  Buffer::OwnedImpl wire_buffer;
  // We populate these proto requests and then encode them into wire_buffer.
  std::vector<std::unique_ptr<helloworld::HelloRequest>> requests;
  // Bounding the size of each request somewhat for sanity sake. We are trading
  // off being able to see what happens at really large sizes, e.g. 16MB, but
  // that will just be too slow, lots of memcpy.
  const size_t MaxRequestNameSize = 96 * 1024;
  for (uint32_t i = 0; i < num_encode_frames; ++i) {
    requests.emplace_back(new helloworld::HelloRequest());
    requests.back()->set_name_bytes(provider.ConsumeRandomLengthString(MaxRequestNameSize));
    // Encode the proto to bytes.
    const std::string request_buffer = requests.back()->SerializeAsString();
    // Encode the gRPC header.
    std::array<uint8_t, 5> header;
    Encoder encoder;
    encoder.newFrame(GRPC_FH_DEFAULT, request_buffer.size(), header);
    // Add header and byte representation of request to wire.
    wire_buffer.add(header.data(), 5);
    wire_buffer.add(request_buffer.data(), request_buffer.size());
  }

  // Add random crud at the end to see if we can make the decoder unhappy in
  // non-standard ways.
  {
    const std::string crud = provider.ConsumeRandomLengthString(MaxRequestNameSize);
    wire_buffer.add(crud.data(), crud.size());
  }

  Decoder decoder;
  std::vector<Frame> frames;
  // We now decode the wire contents, piecemeal.
  while (wire_buffer.length() > 0) {
    // We'll try and pick a partition for the remaining content, so that we
    // enable the fuzzer to explore different ways to cut it.
    const uint64_t decode_length =
        provider.remaining_bytes() == 0
            ? wire_buffer.length()
            : provider.ConsumeIntegralInRange<uint64_t>(0, wire_buffer.length());
    Buffer::OwnedImpl decode_buffer;
    decode_buffer.move(wire_buffer, decode_length);
    const absl::Status decode_result = decoder.decode(decode_buffer, frames);
    // If we have recovered the original frames, we're decoding garbage. It
    // might end up being a valid frame, but there is no predictability, so just
    // drain and move on. If we haven't recovered the original frames, we
    // shouldn't have any errors and should be consuming all of decode_buffer.
    if (frames.size() >= num_encode_frames) {
      decode_buffer.drain(decode_buffer.length());
    } else {
      FUZZ_ASSERT(decode_result.ok());
      FUZZ_ASSERT(decode_buffer.length() == 0);
    }
  }

  // Verify that the original requests are correctly decoded.
  FUZZ_ASSERT(frames.size() >= num_encode_frames);
  for (uint32_t i = 0; i < num_encode_frames; ++i) {
    helloworld::HelloRequest decoded_request;
    FUZZ_ASSERT(decoded_request.ParseFromArray(
        frames[i].data_->linearize(frames[i].data_->length()), frames[i].data_->length()));
    FUZZ_ASSERT(decoded_request.name_bytes() == requests[i]->name_bytes());
  }
}

} // namespace Fuzz
} // namespace Grpc
} // namespace Envoy
