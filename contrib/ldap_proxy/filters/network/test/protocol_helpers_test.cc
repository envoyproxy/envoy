// © 2026 Nokia
// Licensed under the Apache License 2.0
// SPDX-License-Identifier: Apache-2.0

#include "contrib/ldap_proxy/filters/network/source/protocol_helpers.h"
#include "contrib/ldap_proxy/filters/network/source/protocol_templates.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace LdapProxy {
namespace {

class ProtocolHelpersTest : public testing::Test {
protected:
  void SetUp() override {}
};

// extractBerLength tests

TEST_F(ProtocolHelpersTest, ExtractBerLengthShortForm) {
  // Short form: length 0-127 encoded in single byte
  uint8_t data[] = {0x05};  // length = 5
  auto result = extractBerLength(data, sizeof(data), 0);
  
  EXPECT_TRUE(result.valid);
  EXPECT_FALSE(result.need_more_data);
  EXPECT_EQ(5, result.length);
  EXPECT_EQ(1, result.header_bytes);
}

TEST_F(ProtocolHelpersTest, ExtractBerLengthShortFormZero) {
  uint8_t data[] = {0x00};  // length = 0
  auto result = extractBerLength(data, sizeof(data), 0);
  
  EXPECT_TRUE(result.valid);
  EXPECT_EQ(0, result.length);
  EXPECT_EQ(1, result.header_bytes);
}

TEST_F(ProtocolHelpersTest, ExtractBerLengthShortFormMax) {
  uint8_t data[] = {0x7F};  // length = 127
  auto result = extractBerLength(data, sizeof(data), 0);
  
  EXPECT_TRUE(result.valid);
  EXPECT_EQ(127, result.length);
  EXPECT_EQ(1, result.header_bytes);
}

TEST_F(ProtocolHelpersTest, ExtractBerLengthLongFormOneByte) {
  // Long form: 0x81 means 1 length byte follows
  uint8_t data[] = {0x81, 0x80};  // length = 128
  auto result = extractBerLength(data, sizeof(data), 0);
  
  EXPECT_TRUE(result.valid);
  EXPECT_EQ(128, result.length);
  EXPECT_EQ(2, result.header_bytes);
}

TEST_F(ProtocolHelpersTest, ExtractBerLengthLongFormTwoBytes) {
  // 0x82 means 2 length bytes follow
  uint8_t data[] = {0x82, 0x01, 0x00};  // length = 256
  auto result = extractBerLength(data, sizeof(data), 0);
  
  EXPECT_TRUE(result.valid);
  EXPECT_EQ(256, result.length);
  EXPECT_EQ(3, result.header_bytes);
}

TEST_F(ProtocolHelpersTest, ExtractBerLengthLongFormFourBytes) {
  // 0x84 means 4 length bytes follow
  uint8_t data[] = {0x84, 0x00, 0x00, 0x10, 0x00};  // length = 4096
  auto result = extractBerLength(data, sizeof(data), 0);
  
  EXPECT_TRUE(result.valid);
  EXPECT_EQ(4096, result.length);
  EXPECT_EQ(5, result.header_bytes);
}

TEST_F(ProtocolHelpersTest, ExtractBerLengthNeedMoreData) {
  uint8_t data[] = {0x82, 0x01};  // missing second length byte
  auto result = extractBerLength(data, sizeof(data), 0);
  
  EXPECT_FALSE(result.valid);
  EXPECT_TRUE(result.need_more_data);
}

TEST_F(ProtocolHelpersTest, ExtractBerLengthEmptyBuffer) {
  uint8_t data[] = {0x00};
  auto result = extractBerLength(data, 0, 0);  // data_len = 0
  
  EXPECT_FALSE(result.valid);
  EXPECT_TRUE(result.need_more_data);
}

TEST_F(ProtocolHelpersTest, ExtractBerLengthOffsetOutOfBounds) {
  uint8_t data[] = {0x05};
  auto result = extractBerLength(data, sizeof(data), 5);  // offset > data_len
  
  EXPECT_FALSE(result.valid);
  EXPECT_TRUE(result.need_more_data);
}

TEST_F(ProtocolHelpersTest, ExtractBerLengthIndefiniteFormRejected) {
  // 0x80 = indefinite length form (not supported)
  uint8_t data[] = {0x80};
  auto result = extractBerLength(data, sizeof(data), 0);
  
  EXPECT_FALSE(result.valid);
  EXPECT_FALSE(result.need_more_data);
}

TEST_F(ProtocolHelpersTest, ExtractBerLengthTooManyLengthBytes) {
  // 0x85 = 5 length bytes (we only support up to 4)
  uint8_t data[] = {0x85, 0x00, 0x00, 0x00, 0x00, 0x01};
  auto result = extractBerLength(data, sizeof(data), 0);
  
  EXPECT_FALSE(result.valid);
  EXPECT_FALSE(result.need_more_data);
}

TEST_F(ProtocolHelpersTest, ExtractBerLengthExceedsMaxSize) {
  // Length exceeds kMaxInspectBufferSize (16KB)
  uint8_t data[] = {0x83, 0x01, 0x00, 0x00};  // length = 65536
  auto result = extractBerLength(data, sizeof(data), 0);
  
  EXPECT_FALSE(result.valid);
  EXPECT_FALSE(result.need_more_data);
}

// extractMessageId tests

TEST_F(ProtocolHelpersTest, ExtractMessageIdSimple) {
  // LDAP message with messageID = 1
  // SEQUENCE { INTEGER(1) ... }
  uint8_t data[] = {
    0x30, 0x05,  // SEQUENCE, length 5
    0x02, 0x01, 0x01,  // INTEGER, length 1, value 1
    0x00, 0x00  // some trailing data
  };
  
  auto result = extractMessageId(data, sizeof(data));
  
  EXPECT_TRUE(result.valid);
  EXPECT_EQ(1, result.message_id);
  EXPECT_EQ(5, result.total_bytes);
}

TEST_F(ProtocolHelpersTest, ExtractMessageIdLargeValue) {
  // messageID = 0x12345678
  uint8_t data[] = {
    0x30, 0x08,  // SEQUENCE, length 8
    0x02, 0x04, 0x12, 0x34, 0x56, 0x78,  // INTEGER, length 4
    0x00, 0x00
  };
  
  auto result = extractMessageId(data, sizeof(data));
  
  EXPECT_TRUE(result.valid);
  EXPECT_EQ(0x12345678, result.message_id);
}

TEST_F(ProtocolHelpersTest, ExtractMessageIdNotSequence) {
  uint8_t data[] = {0x02, 0x01, 0x01};  // INTEGER, not SEQUENCE
  
  auto result = extractMessageId(data, sizeof(data));
  
  EXPECT_FALSE(result.valid);
}

TEST_F(ProtocolHelpersTest, ExtractMessageIdTooShort) {
  uint8_t data[] = {0x30};  // only 1 byte
  
  auto result = extractMessageId(data, sizeof(data));
  
  EXPECT_FALSE(result.valid);
}

TEST_F(ProtocolHelpersTest, ExtractMessageIdNoInteger) {
  // SEQUENCE containing something other than INTEGER
  uint8_t data[] = {
    0x30, 0x03,  // SEQUENCE
    0x04, 0x01, 0x00  // OCTET STRING instead of INTEGER
  };
  
  auto result = extractMessageId(data, sizeof(data));
  
  EXPECT_FALSE(result.valid);
}

TEST_F(ProtocolHelpersTest, ExtractMessageIdZeroLengthInteger) {
  uint8_t data[] = {
    0x30, 0x02,  // SEQUENCE
    0x02, 0x00   // INTEGER with length 0
  };
  
  auto result = extractMessageId(data, sizeof(data));
  
  EXPECT_FALSE(result.valid);
}

TEST_F(ProtocolHelpersTest, ExtractMessageIdIntegerTooLong) {
  uint8_t data[] = {
    0x30, 0x07,  // SEQUENCE
    0x02, 0x05, 0x01, 0x02, 0x03, 0x04, 0x05  // 5-byte INTEGER
  };
  
  auto result = extractMessageId(data, sizeof(data));
  
  EXPECT_FALSE(result.valid);
}

// isCompleteBerMessage tests

TEST_F(ProtocolHelpersTest, IsCompleteBerMessageComplete) {
  uint8_t data[] = {
    0x30, 0x05,  // SEQUENCE, length 5
    0x01, 0x02, 0x03, 0x04, 0x05  // 5 bytes of content
  };
  
  auto [complete, length] = isCompleteBerMessage(data, sizeof(data));
  
  EXPECT_TRUE(complete);
  EXPECT_EQ(7, length);  // 1 (tag) + 1 (length) + 5 (content)
}

TEST_F(ProtocolHelpersTest, IsCompleteBerMessageIncomplete) {
  uint8_t data[] = {
    0x30, 0x10,  // SEQUENCE, length 16
    0x01, 0x02, 0x03  // only 3 bytes, need 16
  };
  
  auto [complete, length] = isCompleteBerMessage(data, sizeof(data));
  
  EXPECT_FALSE(complete);
  EXPECT_EQ(0, length);
}

TEST_F(ProtocolHelpersTest, IsCompleteBerMessageNotSequence) {
  uint8_t data[] = {0x02, 0x01, 0x01};  // INTEGER
  
  auto [complete, length] = isCompleteBerMessage(data, sizeof(data));
  
  EXPECT_FALSE(complete);
}

TEST_F(ProtocolHelpersTest, IsCompleteBerMessageTooShort) {
  uint8_t data[] = {0x30};  // only tag
  
  auto [complete, length] = isCompleteBerMessage(data, sizeof(data));
  
  EXPECT_FALSE(complete);
}

// encodeBerInteger tests

TEST_F(ProtocolHelpersTest, EncodeBerIntegerZero) {
  auto encoded = encodeBerInteger(0);
  
  EXPECT_EQ(3, encoded.size());
  EXPECT_EQ(0x02, encoded[0]);  // INTEGER tag
  EXPECT_EQ(0x01, encoded[1]);  // length 1
  EXPECT_EQ(0x00, encoded[2]);  // value 0
}

TEST_F(ProtocolHelpersTest, EncodeBerIntegerSmall) {
  auto encoded = encodeBerInteger(1);
  
  EXPECT_EQ(3, encoded.size());
  EXPECT_EQ(0x02, encoded[0]);
  EXPECT_EQ(0x01, encoded[1]);
  EXPECT_EQ(0x01, encoded[2]);
}

TEST_F(ProtocolHelpersTest, EncodeBerIntegerNeedsLeadingZero) {
  // 128 (0x80) needs leading zero because high bit is set
  auto encoded = encodeBerInteger(128);
  
  EXPECT_EQ(4, encoded.size());
  EXPECT_EQ(0x02, encoded[0]);
  EXPECT_EQ(0x02, encoded[1]);  // length 2
  EXPECT_EQ(0x00, encoded[2]);  // leading zero
  EXPECT_EQ(0x80, encoded[3]);
}

TEST_F(ProtocolHelpersTest, EncodeBerIntegerTwoBytes) {
  auto encoded = encodeBerInteger(0x0102);
  
  EXPECT_EQ(4, encoded.size());
  EXPECT_EQ(0x02, encoded[0]);
  EXPECT_EQ(0x02, encoded[1]);
  EXPECT_EQ(0x01, encoded[2]);
  EXPECT_EQ(0x02, encoded[3]);
}

TEST_F(ProtocolHelpersTest, EncodeBerIntegerFourBytes) {
  auto encoded = encodeBerInteger(0x01020304);
  
  EXPECT_EQ(6, encoded.size());
  EXPECT_EQ(0x02, encoded[0]);
  EXPECT_EQ(0x04, encoded[1]);
  EXPECT_EQ(0x01, encoded[2]);
  EXPECT_EQ(0x02, encoded[3]);
  EXPECT_EQ(0x03, encoded[4]);
  EXPECT_EQ(0x04, encoded[5]);
}

// parseExtendedResponse tests

TEST_F(ProtocolHelpersTest, ParseExtendedResponseSuccess) {
  // Valid ExtendedResponse with resultCode=0, msgID=1
  uint8_t data[] = {
    0x30, 0x0c,        // SEQUENCE
    0x02, 0x01, 0x01,  // INTEGER msgID=1
    0x78, 0x07,        // ExtendedResponse [APPLICATION 24]
    0x0a, 0x01, 0x00,  // ENUMERATED resultCode=0
    0x04, 0x00,        // matchedDN=""
    0x04, 0x00         // diagnosticMessage=""
  };
  
  uint8_t result_code = 255;
  uint32_t msg_id = 0;
  
  bool ok = parseExtendedResponse(data, sizeof(data), result_code, msg_id);
  
  EXPECT_TRUE(ok);
  EXPECT_EQ(0, result_code);
  EXPECT_EQ(1, msg_id);
}

TEST_F(ProtocolHelpersTest, ParseExtendedResponseErrorCode) {
  // ExtendedResponse with resultCode=13 (confidentialityRequired)
  uint8_t data[] = {
    0x30, 0x0c,
    0x02, 0x01, 0x05,  // msgID=5
    0x78, 0x07,
    0x0a, 0x01, 0x0d,  // resultCode=13
    0x04, 0x00,
    0x04, 0x00
  };
  
  uint8_t result_code = 0;
  uint32_t msg_id = 0;
  
  bool ok = parseExtendedResponse(data, sizeof(data), result_code, msg_id);
  
  EXPECT_TRUE(ok);
  EXPECT_EQ(13, result_code);
  EXPECT_EQ(5, msg_id);
}

TEST_F(ProtocolHelpersTest, ParseExtendedResponseTooShort) {
  uint8_t data[] = {0x30, 0x05, 0x02, 0x01};  // incomplete
  
  uint8_t result_code = 0;
  uint32_t msg_id = 0;
  
  bool ok = parseExtendedResponse(data, sizeof(data), result_code, msg_id);
  
  EXPECT_FALSE(ok);
}

TEST_F(ProtocolHelpersTest, ParseExtendedResponseWrongTag) {
  // Not a SEQUENCE
  uint8_t data[] = {
    0x02, 0x0c,        // INTEGER instead of SEQUENCE
    0x02, 0x01, 0x01,
    0x78, 0x07,
    0x0a, 0x01, 0x00,
    0x04, 0x00,
    0x04, 0x00
  };
  
  uint8_t result_code = 0;
  uint32_t msg_id = 0;
  
  bool ok = parseExtendedResponse(data, sizeof(data), result_code, msg_id);
  
  EXPECT_FALSE(ok);
}

// createStartTlsSuccessResponse tests

TEST_F(ProtocolHelpersTest, CreateStartTlsSuccessResponse) {
  auto response = createStartTlsSuccessResponse(1);
  
  EXPECT_FALSE(response.empty());
  EXPECT_EQ(0x30, response[0]);  // SEQUENCE
  
  // Parse the response to verify it's valid
  uint8_t result_code = 255;
  uint32_t msg_id = 0;
  bool ok = parseExtendedResponse(response.data(), response.size(), result_code, msg_id);
  
  EXPECT_TRUE(ok);
  EXPECT_EQ(0, result_code);
  EXPECT_EQ(1, msg_id);
}

TEST_F(ProtocolHelpersTest, CreateStartTlsSuccessResponseLargeMsgId) {
  auto response = createStartTlsSuccessResponse(0x12345678);
  
  uint8_t result_code = 255;
  uint32_t msg_id = 0;
  bool ok = parseExtendedResponse(response.data(), response.size(), result_code, msg_id);
  
  EXPECT_TRUE(ok);
  EXPECT_EQ(0, result_code);
  EXPECT_EQ(0x12345678, msg_id);
}

// createStartTlsRequest tests

TEST_F(ProtocolHelpersTest, CreateStartTlsRequest) {
  auto request = createStartTlsRequest(1);
  
  EXPECT_FALSE(request.empty());
  EXPECT_EQ(0x30, request[0]);  // SEQUENCE
  
  // Verify it contains the StartTLS OID
  std::string oid_str = "1.3.6.1.4.1.1466.20037";
  std::string request_str(request.begin(), request.end());
  EXPECT_NE(std::string::npos, request_str.find(oid_str));
}

}  // namespace
}  // namespace LdapProxy
}  // namespace NetworkFilters
}  // namespace Extensions
}  // namespace Envoy
