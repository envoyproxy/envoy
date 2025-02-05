#include "source/common/common/logger.h"
#include "source/common/proxy_protocol/proxy_protocol.h"

#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Common {
namespace ProxyProtocol {
namespace {

TEST(ProxyProtocolHeaderTest, ParseTLVs) {
  Protobuf::RepeatedPtrField<envoy::config::core::v3::ProxyProtocolTLV> tlvs;
  auto* tlv = tlvs.Add();
  tlv->set_type(0x1);
  tlv->set_value("tlv1");
  tlv = tlvs.Add();
  tlv->set_type(0xE1);
  tlv->set_value("tlv2");
  const auto tlv_vector = parseTLVs(tlvs);

  EXPECT_EQ(2, tlv_vector.size());
  EXPECT_EQ(0x1, tlv_vector[0].type);
  EXPECT_EQ("tlv1", std::string(tlv_vector[0].value.begin(), tlv_vector[0].value.end()));
  EXPECT_EQ(0xE1, tlv_vector[1].type);
  EXPECT_EQ("tlv2", std::string(tlv_vector[1].value.begin(), tlv_vector[1].value.end()));
}

} // namespace
} // namespace ProxyProtocol
} // namespace Common
} // namespace Envoy
