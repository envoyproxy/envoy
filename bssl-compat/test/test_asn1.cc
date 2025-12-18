#include <gtest/gtest.h>
#include <openssl/asn1.h>
#include <openssl/mem.h>
#include <openssl/bytestring.h>
#include <iostream>


static const char TestString[] = { "TestString" };
static const int TestStringLength = strlen(TestString);

TEST(Asn1Test, asn1string) {
    // Testing ASN1_IA5STRING_new
  ASN1_IA5STRING *ia5TestString = ASN1_IA5STRING_new();
  EXPECT_TRUE(ia5TestString != nullptr);
    // Testing ASN1_STRING_set
  ASN1_STRING_set(ia5TestString, TestString, TestStringLength);
  EXPECT_EQ(ASN1_STRING_length(ia5TestString), TestStringLength);
    // Testing ASN1_STRING_data
  EXPECT_STREQ((const char *)ASN1_STRING_data(ia5TestString), TestString);
    // Testing ASN1_STRING_get0_data
  EXPECT_STREQ((const char *)ASN1_STRING_get0_data(ia5TestString),TestString);
    // Testing ia5TestString
  ASN1_IA5STRING_free(ia5TestString);
}


TEST(Asn1Test, c2i_ASN1_INTEGER_to_BN_to_hex) {
  // ASN.1 encoding is big endian, so we can't just pass the bytes from a C++
  // int or long, as laid out in memory, to c2i_ASN1_INTEGER() because we may
  // be running on a little or big endian machine. Therefore, we use arrays
  // of uint8_t bytes that are explicitly laid out in big endian order.

  std::map<std::vector<uint8_t>,const char*> values {
    { { 0x00 }, "0" },
    { { 0x12 }, "12" },
    { { 0xEC }, "-14" },
    { { 0x12, 0x34 }, "1234" },
    { { 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08 }, "0102030405060708" },
  };

  for (auto const& [bytes, hex] : values) {
    const uint8_t *pbytes {bytes.data()};
    ASN1_INTEGER *asn1int {nullptr};

    ASSERT_EQ(c2i_ASN1_INTEGER(&asn1int, &pbytes, bytes.size()), asn1int);
    ASSERT_NE(nullptr, asn1int) << ERR_error_string(ERR_get_error(), nullptr);
    ASSERT_EQ(bytes.data() + bytes.size(), pbytes);

    BIGNUM *bignum = ASN1_INTEGER_to_BN(asn1int, nullptr);
    ASSERT_NE(nullptr, bignum);

    char *str = BN_bn2hex(bignum);
    ASSERT_NE(nullptr, str);
    ASSERT_STREQ(hex, str);

    OPENSSL_free(str);
    BN_free(bignum);
    ASN1_INTEGER_free(asn1int);
  }
}


static const time_t SecsInAnHour=3600;
static const int HoursInADay=24;
// In this test the diff is 5 days and 17 seconds
static const int TestNumDays=5;
static const int TestNumSecs=17;

TEST(Asn1Test, asn1time) {
    // Test ASN1_TIME_new
  ASN1_TIME *asn1TestTime = ASN1_TIME_new();
  EXPECT_TRUE(asn1TestTime != nullptr);
  time_t from=0;
    // Test ASN1_TIME_set
  EXPECT_TRUE(ASN1_TIME_set(asn1TestTime,from) != nullptr);
  ASN1_TIME *asn1TestTimeTo = ASN1_TIME_new();
  EXPECT_TRUE(asn1TestTimeTo != nullptr);
  time_t to = SecsInAnHour * HoursInADay * TestNumDays + TestNumSecs;
  EXPECT_TRUE(ASN1_TIME_set(asn1TestTimeTo,to) != nullptr);
  int out_days=0, out_seconds=0;
    // Test ASN1_TIME_diff
  EXPECT_EQ(ASN1_TIME_diff(&out_days, &out_seconds,
                           asn1TestTime, asn1TestTimeTo), 1);
  EXPECT_TRUE(out_days==TestNumDays && out_seconds==TestNumSecs);
    // Test ASN1_TIME_free
  ASN1_TIME_free(asn1TestTime);
  ASN1_TIME_free(asn1TestTimeTo);
}
