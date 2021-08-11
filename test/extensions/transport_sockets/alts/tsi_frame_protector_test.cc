#include "source/common/buffer/buffer_impl.h"
#include "source/extensions/transport_sockets/alts/tsi_frame_protector.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/core/tsi/fake_transport_security.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Alts {
namespace {

using namespace std::string_literals;

/**
 * Test with fake frame protector. The protected frame header is 4 byte length (little endian,
 * include header itself) and following the body.
 */
class TsiFrameProtectorTest : public testing::Test {
public:
  TsiFrameProtectorTest()
      : raw_frame_protector_(tsi_create_fake_zero_copy_grpc_protector(nullptr)),
        frame_protector_(CFrameProtectorPtr{raw_frame_protector_}) {}

protected:
  tsi_zero_copy_grpc_protector* raw_frame_protector_;
  TsiFrameProtector frame_protector_;
};

TEST_F(TsiFrameProtectorTest, Protect) {
  {
    Buffer::OwnedImpl encrypted;

    EXPECT_EQ(TSI_OK, frame_protector_.protect(grpc_slice_from_static_string("foo"), encrypted));
    EXPECT_EQ("\x07\0\0\0foo"s, encrypted.toString());
  }

  {
    Buffer::OwnedImpl encrypted;

    EXPECT_EQ(TSI_OK, frame_protector_.protect(grpc_slice_from_static_string("foo"), encrypted));
    EXPECT_EQ("\x07\0\0\0foo"s, encrypted.toString());

    EXPECT_EQ(TSI_OK, frame_protector_.protect(grpc_slice_from_static_string("bar"), encrypted));
    EXPECT_EQ("\x07\0\0\0foo\x07\0\0\0bar"s, encrypted.toString());
  }

  {
    Buffer::OwnedImpl encrypted;

    EXPECT_EQ(TSI_OK,
              frame_protector_.protect(
                  grpc_slice_from_static_string(std::string(20000, 'a').c_str()), encrypted));

    // fake frame protector will split long buffer to 2 "encrypted" frames with length 16K.
    std::string expected =
        "\0\x40\0\0"s + std::string(16380, 'a') + "\x28\x0e\0\0"s + std::string(3620, 'a');
    EXPECT_EQ(expected, encrypted.toString());
  }
}

TEST_F(TsiFrameProtectorTest, ProtectError) {
  const tsi_zero_copy_grpc_protector_vtable* vtable = raw_frame_protector_->vtable;
  tsi_zero_copy_grpc_protector_vtable mock_vtable = *raw_frame_protector_->vtable;
  mock_vtable.protect = [](tsi_zero_copy_grpc_protector*, grpc_slice_buffer*, grpc_slice_buffer*) {
    return TSI_INTERNAL_ERROR;
  };
  raw_frame_protector_->vtable = &mock_vtable;

  Buffer::OwnedImpl encrypted;

  EXPECT_EQ(TSI_INTERNAL_ERROR,
            frame_protector_.protect(grpc_slice_from_static_string("foo"), encrypted));

  raw_frame_protector_->vtable = vtable;
}

TEST_F(TsiFrameProtectorTest, Unprotect) {
  {
    Buffer::OwnedImpl input, decrypted;
    input.add("\x07\0\0\0bar"s);

    EXPECT_EQ(TSI_OK, frame_protector_.unprotect(input, decrypted));
    EXPECT_EQ("bar", decrypted.toString());
  }

  {
    Buffer::OwnedImpl input, decrypted;
    input.add("\x0a\0\0\0foo"s);

    EXPECT_EQ(TSI_OK, frame_protector_.unprotect(input, decrypted));
    EXPECT_EQ("", decrypted.toString());

    input.add("bar");
    EXPECT_EQ(TSI_OK, frame_protector_.unprotect(input, decrypted));
    EXPECT_EQ("foobar", decrypted.toString());
  }

  {
    Buffer::OwnedImpl input, decrypted;
    input.add("\0\x40\0\0"s + std::string(16380, 'a'));
    input.add("\x28\x0e\0\0"s + std::string(3620, 'a'));

    EXPECT_EQ(TSI_OK, frame_protector_.unprotect(input, decrypted));
    EXPECT_EQ(std::string(20000, 'a'), decrypted.toString());
  }
}
TEST_F(TsiFrameProtectorTest, UnprotectError) {
  const tsi_zero_copy_grpc_protector_vtable* vtable = raw_frame_protector_->vtable;
  tsi_zero_copy_grpc_protector_vtable mock_vtable = *raw_frame_protector_->vtable;
  mock_vtable.unprotect = [](tsi_zero_copy_grpc_protector*, grpc_slice_buffer*,
                             grpc_slice_buffer*) { return TSI_INTERNAL_ERROR; };
  raw_frame_protector_->vtable = &mock_vtable;

  Buffer::OwnedImpl input, decrypted;
  input.add("\x0a\0\0\0foo"s);

  EXPECT_EQ(TSI_INTERNAL_ERROR, frame_protector_.unprotect(input, decrypted));

  raw_frame_protector_->vtable = vtable;
}

} // namespace
} // namespace Alts
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
