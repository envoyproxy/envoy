#include <limits>

#include "source/common/tls/ocsp/asn1_utility.h"

#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {
namespace Ocsp {

namespace {

class Asn1UtilityTest : public testing::Test {
public:
  // DER encoding of a single TLV `ASN.1` element.
  // returns a pointer to the underlying buffer and transfers
  // ownership to the caller.
  uint8_t* asn1Encode(CBS& cbs, std::string& value, unsigned tag) {
    bssl::ScopedCBB cbb;
    CBB child;
    auto data_head = reinterpret_cast<const uint8_t*>(value.c_str());

    EXPECT_TRUE(CBB_init(cbb.get(), 0));
    EXPECT_TRUE(CBB_add_asn1(cbb.get(), &child, tag));
    EXPECT_TRUE(CBB_add_bytes(&child, data_head, value.size()));

    uint8_t* buf;
    size_t buf_len;
    EXPECT_TRUE(CBB_finish(cbb.get(), &buf, &buf_len));

    CBS_init(&cbs, buf, buf_len);
    return buf;
  }

  template <typename T>
  void expectParseResultErrorOnWrongTag(std::function<absl::StatusOr<T>(CBS&)> parse) {
    CBS cbs;
    CBS_init(&cbs, asn1_true.data(), asn1_true.size());
    EXPECT_FALSE(parse(cbs).status().ok());
  }

  const std::vector<uint8_t> asn1_true = {0x1u, 1, 0xff};
  const std::vector<uint8_t> asn1_empty_seq = {0x30, 0};
};

TEST_F(Asn1UtilityTest, ParseMethodsWrongTagTest) {
  expectParseResultErrorOnWrongTag<std::vector<std::vector<uint8_t>>>([](CBS& cbs) {
    return Asn1Utility::parseSequenceOf<std::vector<uint8_t>>(cbs, Asn1Utility::parseOctetString);
  });
  expectParseResultErrorOnWrongTag<std::string>(Asn1Utility::parseOid);
  expectParseResultErrorOnWrongTag<Envoy::SystemTime>(Asn1Utility::parseGeneralizedTime);
  expectParseResultErrorOnWrongTag<std::string>(Asn1Utility::parseInteger);
  expectParseResultErrorOnWrongTag<std::vector<uint8_t>>(Asn1Utility::parseOctetString);
}

TEST_F(Asn1UtilityTest, ToStringTest) {
  CBS cbs;
  absl::string_view str = "test";
  CBS_init(&cbs, reinterpret_cast<const uint8_t*>(str.data()), str.size());
  EXPECT_EQ(str, Asn1Utility::cbsToString(cbs));
}

TEST_F(Asn1UtilityTest, ParseSequenceOfEmptySequenceTest) {
  CBS cbs;
  CBS_init(&cbs, asn1_empty_seq.data(), asn1_empty_seq.size());

  std::vector<std::vector<uint8_t>> vec;
  auto actual =
      Asn1Utility::parseSequenceOf<std::vector<uint8_t>>(cbs, Asn1Utility::parseOctetString)
          .value();
  EXPECT_EQ(vec, actual);
}

TEST_F(Asn1UtilityTest, ParseSequenceOfMultipleElementSequenceTest) {
  std::vector<uint8_t> octet_seq = {
      // SEQUENCE OF 3 2-byte elements
      0x30,
      3 * (2 + 2),
      // 1st OCTET STRING
      0x4u,
      2,
      0x1,
      0x2,
      // 2nd OCTET STRING
      0x4u,
      2,
      0x3,
      0x4,
      // 3rd OCTET STRING
      0x4u,
      2,
      0x5,
      0x6,
  };
  CBS cbs;
  CBS_init(&cbs, octet_seq.data(), octet_seq.size());

  std::vector<std::vector<uint8_t>> vec = {{0x1, 0x2}, {0x3, 0x4}, {0x5, 0x6}};
  auto actual =
      Asn1Utility::parseSequenceOf<std::vector<uint8_t>>(cbs, Asn1Utility::parseOctetString)
          .value();
  EXPECT_EQ(vec, actual);
}

TEST_F(Asn1UtilityTest, SequenceOfLengthMismatchErrorTest) {
  std::vector<uint8_t> malformed = {
      // SEQUENCE OF length wrongfully 2 instead of 4 bytes
      0x30,
      3,
      // 1st OCTET STRING
      0x4u,
      2,
      0x1,
      0x2,
  };
  CBS cbs;
  CBS_init(&cbs, malformed.data(), malformed.size());

  EXPECT_EQ("Input is not a well-formed ASN.1 OCTETSTRING",
            Asn1Utility::parseSequenceOf<std::vector<uint8_t>>(cbs, Asn1Utility::parseOctetString)
                .status()
                .message());
}

TEST_F(Asn1UtilityTest, SequenceOfMixedTypeErrorTest) {
  std::vector<uint8_t> mixed_type = {
      // SEQUENCE OF 1 OCTET STRING and 1 BOOLEAN
      0x30,
      7,
      // OCTET STRING
      0x4u,
      2,
      0x1,
      0x2,
      // BOOLEAN true
      0x1u,
      1,
      0xff,
  };
  CBS cbs;
  CBS_init(&cbs, mixed_type.data(), mixed_type.size());

  EXPECT_EQ("Input is not a well-formed ASN.1 OCTETSTRING",
            Asn1Utility::parseSequenceOf<std::vector<uint8_t>>(cbs, Asn1Utility::parseOctetString)
                .status()
                .message());
}

TEST_F(Asn1UtilityTest, GetOptionalTest) {
  CBS cbs;
  CBS_init(&cbs, asn1_true.data(), asn1_true.size());

  const uint8_t* start = CBS_data(&cbs);
  EXPECT_EQ(absl::nullopt, Asn1Utility::getOptional(cbs, CBS_ASN1_INTEGER).value());
  EXPECT_EQ(start, CBS_data(&cbs));

  CBS value = Asn1Utility::getOptional(cbs, CBS_ASN1_BOOLEAN).value().value();
  EXPECT_EQ(0xff, *CBS_data(&value));
}

TEST_F(Asn1UtilityTest, GetOptionalMissingValueTest) {
  std::vector<uint8_t> missing_val_bool = {0x1u, 1};
  CBS cbs;
  CBS_init(&cbs, missing_val_bool.data(), missing_val_bool.size());

  auto res = Asn1Utility::getOptional(cbs, CBS_ASN1_BOOLEAN);
  EXPECT_EQ("Failed to parse ASN.1 element tag", res.status().message());
}

TEST_F(Asn1UtilityTest, ParseOptionalTest) {
  std::vector<uint8_t> nothing;
  std::vector<uint8_t> explicit_optional_true = {0xa0, 3, 0x1u, 1, 0xff};
  std::vector<uint8_t> missing_val_bool = {0x1u, 1};

  auto parse_bool = [](CBS& cbs) -> bool {
    int res;
    CBS_get_asn1_bool(&cbs, &res);
    return res;
  };

  auto parse_bool_fail = [](CBS&) -> absl::StatusOr<bool> {
    std::cout << "failing" << std::endl;
    return absl::InvalidArgumentError("failed");
  };

  {
    CBS cbs_explicit_optional_true;
    CBS_init(&cbs_explicit_optional_true, explicit_optional_true.data(),
             explicit_optional_true.size());

    absl::optional<bool> expected(true);
    EXPECT_EQ(expected,
              Asn1Utility::parseOptional<bool>(cbs_explicit_optional_true, parse_bool,
                                               CBS_ASN1_CONTEXT_SPECIFIC | CBS_ASN1_CONSTRUCTED | 0)
                  .value());
  }

  {
    CBS cbs_empty_seq;
    CBS_init(&cbs_empty_seq, asn1_empty_seq.data(), asn1_empty_seq.size());
    EXPECT_EQ(
        absl::nullopt,
        Asn1Utility::parseOptional<bool>(cbs_empty_seq, parse_bool, CBS_ASN1_BOOLEAN).value());
  }

  {
    CBS cbs_nothing;
    CBS_init(&cbs_nothing, nothing.data(), nothing.size());

    EXPECT_EQ(absl::nullopt,
              Asn1Utility::parseOptional<bool>(cbs_nothing, parse_bool, CBS_ASN1_BOOLEAN).value());
  }

  {
    CBS cbs_missing_val;
    CBS_init(&cbs_missing_val, missing_val_bool.data(), missing_val_bool.size());

    EXPECT_EQ("Failed to parse ASN.1 element tag",
              Asn1Utility::parseOptional<bool>(cbs_missing_val, parse_bool, CBS_ASN1_BOOLEAN)
                  .status()
                  .message());
  }

  {
    CBS cbs_explicit_optional_true;
    CBS_init(&cbs_explicit_optional_true, explicit_optional_true.data(),
             explicit_optional_true.size());

    EXPECT_EQ("failed",
              Asn1Utility::parseOptional<bool>(cbs_explicit_optional_true, parse_bool_fail,
                                               CBS_ASN1_CONTEXT_SPECIFIC | CBS_ASN1_CONSTRUCTED | 0)
                  .status()
                  .message());
  }
}

TEST_F(Asn1UtilityTest, ParseOidTest) {
  std::string oid = "1.1.1.1.1.1.1";

  bssl::ScopedCBB cbb;
  CBB child;
  ASSERT_TRUE(CBB_init(cbb.get(), 0));
  ASSERT_TRUE(CBB_add_asn1(cbb.get(), &child, CBS_ASN1_OBJECT));
  ASSERT_TRUE(CBB_add_asn1_oid_from_text(&child, oid.c_str(), oid.size()));

  uint8_t* buf;
  size_t buf_len;
  CBS cbs;
  ASSERT_TRUE(CBB_finish(cbb.get(), &buf, &buf_len));
  CBS_init(&cbs, buf, buf_len);
  bssl::UniquePtr<uint8_t> scoped(buf);

  EXPECT_EQ(oid, Asn1Utility::parseOid(cbs).value());
}

TEST_F(Asn1UtilityTest, ParseOidInvalidValueTest) {
  // 0x80 is not valid within an OID
  std::vector<uint8_t> invalid_oid = {0x6, 0x6, 0x29, 0x80, 0x1, 0x1, 0x1, 0x1};

  CBS cbs;
  CBS_init(&cbs, invalid_oid.data(), invalid_oid.size());

  EXPECT_EQ("Failed to parse oid", Asn1Utility::parseOid(cbs).status().message());
}

TEST_F(Asn1UtilityTest, ParseGeneralizedTimeWrongFormatErrorTest) {
  std::string invalid_time = "";
  CBS cbs;
  bssl::UniquePtr<uint8_t> scoped(asn1Encode(cbs, invalid_time, CBS_ASN1_GENERALIZEDTIME));
  EXPECT_FALSE(Asn1Utility::parseGeneralizedTime(cbs).status().ok());
  EXPECT_EQ("Input is not a well-formed ASN.1 GENERALIZEDTIME",
            Asn1Utility::parseGeneralizedTime(cbs).status().message());
}

TEST_F(Asn1UtilityTest, ParseGeneralizedTimeTest) {
  std::string time = "20070614185900z";
  std::string expected_time = "20070614185900";

  CBS cbs;
  bssl::UniquePtr<uint8_t> scoped(asn1Encode(cbs, time, CBS_ASN1_GENERALIZEDTIME));
  absl::Time expected = TestUtility::parseTime(expected_time, "%E4Y%m%d%H%M%S");
  auto actual = Asn1Utility::parseGeneralizedTime(cbs).value();

  EXPECT_EQ(absl::ToChronoTime(expected), actual);
}

TEST_F(Asn1UtilityTest, TestParseGeneralizedTimeRejectsNonUTCTime) {
  std::string local_time = "20070601145918";
  CBS cbs;
  bssl::UniquePtr<uint8_t> scoped(asn1Encode(cbs, local_time, CBS_ASN1_GENERALIZEDTIME));

  EXPECT_EQ("GENERALIZEDTIME must be in UTC",
            Asn1Utility::parseGeneralizedTime(cbs).status().message());
}

TEST_F(Asn1UtilityTest, TestParseGeneralizedTimeInvalidTime) {
  std::string ymd = "20070601Z";
  CBS cbs;
  bssl::UniquePtr<uint8_t> scoped(asn1Encode(cbs, ymd, CBS_ASN1_GENERALIZEDTIME));

  EXPECT_EQ("Error parsing string of GENERALIZEDTIME format",
            Asn1Utility::parseGeneralizedTime(cbs).status().message());
}

// Taken from
// https://boringssl.googlesource.com/boringssl/+/master/crypto/bytestring/cbb.c#531
// because boringssl_fips does not yet implement `CBB_add_asn1_int64`
void cbbAddAsn1Int64(CBB* cbb, int64_t value) {
  if (value >= 0) {
    ASSERT_TRUE(CBB_add_asn1_uint64(cbb, value));
  }

  // Skip past bytes that are purely sign extension.
  int start;
  for (start = 7; start > 0; start--) {
    uint8_t byte = (value >> start * 8) & 0xFF;
    if (byte != 0xFF) {
      break;
    }

    uint8_t next_byte = (value >> (start - 1) * 8) & 0xFF;
    if ((next_byte & 0x80) == 0) {
      break;
    }
  }

  CBB child;
  ASSERT_TRUE(CBB_add_asn1(cbb, &child, CBS_ASN1_INTEGER));
  for (int i = start; i >= 0; i--) {
    uint8_t byte = (value >> i * 8) & 0xFF;
    ASSERT_TRUE(CBB_add_u8(&child, byte));
  }
  CBB_flush(cbb);
}

TEST_F(Asn1UtilityTest, ParseIntegerTest) {
  std::vector<std::pair<int64_t, std::string>> integers = {
      {1, "01"}, {10, "0a"}, {1000000, "0f4240"}, {-1, "-01"}, {-128, "-80"},
  };
  bssl::ScopedCBB cbb;
  CBS cbs;
  uint8_t* buf;
  size_t buf_len;
  for (auto const& int_and_hex : integers) {
    ASSERT_TRUE(CBB_init(cbb.get(), 0));
    cbbAddAsn1Int64(cbb.get(), int_and_hex.first);
    ASSERT_TRUE(CBB_finish(cbb.get(), &buf, &buf_len));

    CBS_init(&cbs, buf, buf_len);
    bssl::UniquePtr<uint8_t> scoped_buf(buf);

    EXPECT_EQ(int_and_hex.second, Asn1Utility::parseInteger(cbs).value());
    cbb.Reset();
  }
}

TEST_F(Asn1UtilityTest, ParseOctetStringTest) {
  std::vector<uint8_t> data = {0x1, 0x2, 0x3};
  std::string data_str(data.begin(), data.end());
  CBS cbs;
  bssl::UniquePtr<uint8_t> scoped(asn1Encode(cbs, data_str, CBS_ASN1_OCTETSTRING));

  EXPECT_EQ(data, Asn1Utility::parseOctetString(cbs).value());
}

TEST_F(Asn1UtilityTest, SkipOptionalPresentAdvancesTest) {
  CBS cbs;
  CBS_init(&cbs, asn1_empty_seq.data(), asn1_empty_seq.size());

  const uint8_t* start = CBS_data(&cbs);
  EXPECT_NO_THROW(Asn1Utility::skipOptional(cbs, CBS_ASN1_SEQUENCE).value());
  EXPECT_EQ(start + 2, CBS_data(&cbs));
}

TEST_F(Asn1UtilityTest, SkipOptionalNotPresentDoesNotAdvanceTest) {
  CBS cbs;
  CBS_init(&cbs, asn1_empty_seq.data(), asn1_empty_seq.size());

  const uint8_t* start = CBS_data(&cbs);
  EXPECT_NO_THROW(Asn1Utility::skipOptional(cbs, CBS_ASN1_BOOLEAN).value());
  EXPECT_EQ(start, CBS_data(&cbs));
}

TEST_F(Asn1UtilityTest, SkipOptionalMalformedTagTest) {
  std::vector<uint8_t> malformed_seq = {0x30};
  CBS cbs;
  CBS_init(&cbs, malformed_seq.data(), malformed_seq.size());

  EXPECT_EQ("Failed to parse ASN.1 element tag",
            Asn1Utility::skipOptional(cbs, CBS_ASN1_SEQUENCE).status().message());
}

TEST_F(Asn1UtilityTest, SkipMalformedTagTest) {
  std::vector<uint8_t> malformed_seq = {0x30};
  CBS cbs;
  CBS_init(&cbs, malformed_seq.data(), malformed_seq.size());

  EXPECT_EQ("Failed to parse ASN.1 element",
            Asn1Utility::skip(cbs, CBS_ASN1_SEQUENCE).status().message());
}

} // namespace

} // namespace Ocsp
} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
