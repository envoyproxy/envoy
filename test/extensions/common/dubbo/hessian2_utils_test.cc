#include "envoy/common/exception.h"

#include "source/extensions/common/dubbo/hessian2_utils.h"

#include "test/test_common/printers.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Dubbo {
namespace {

TEST(BufferReaderTest, BufferReaderTest) {
  Envoy::Buffer::OwnedImpl buffer;
  buffer.add("test");

  BufferReader reader_1(buffer);

  EXPECT_EQ(0, reader_1.offset());
  EXPECT_EQ(4, reader_1.length());

  std::string test_string(4, 0);

  reader_1.readNBytes(test_string.data(), 4);

  EXPECT_EQ("test", test_string);
  EXPECT_EQ(4, reader_1.offset());

  BufferReader reader_2(buffer, 2);

  EXPECT_EQ(2, reader_2.offset());
}

TEST(BufferWriterTest, BufferWriterTest) {
  Envoy::Buffer::OwnedImpl buffer;
  buffer.add("test");

  BufferWriter writer_1(buffer);

  std::string test_string("123456789");

  writer_1.rawWrite(test_string);

  writer_1.rawWrite(test_string.data(), 5);

  EXPECT_EQ("test12345678912345", buffer.toString());
}

TEST(Hessian2UtilsTest, GetParametersNumberTest) {
  const std::string test_empty_types = "";

  EXPECT_EQ(0, Hessian2Utils::getParametersNumber(test_empty_types));

  // "[B" stands for binary type.
  const std::string test_normal_types = "VZ[BBCDFIJS";

  EXPECT_EQ(10, Hessian2Utils::getParametersNumber(test_normal_types));

  const std::string test_with_arrays = "[V[Z[B[C[D[F[I[J[S";

  EXPECT_EQ(9, Hessian2Utils::getParametersNumber(test_with_arrays));

  const std::string test_with_object = "Ljava.lang.String;[BCDLjava.lang.Date;CDFIJ";

  EXPECT_EQ(10, Hessian2Utils::getParametersNumber(test_with_object));

  const std::string test_hybrid_types =
      "CD[B[Ljava.lang.String;[Ljava.lang.Date;DLjava.lang.String;FIJ";

  EXPECT_EQ(10, Hessian2Utils::getParametersNumber(test_hybrid_types));

  const std::string test_error_types = "abcdefg";

  EXPECT_EQ(0, Hessian2Utils::getParametersNumber(test_error_types));
}

} // namespace
} // namespace Dubbo
} // namespace Common
} // namespace Extensions
} // namespace Envoy
