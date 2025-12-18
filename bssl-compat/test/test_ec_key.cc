#include <gtest/gtest.h>
#include <openssl/ec.h>
#include <openssl/bytestring.h>
#include <openssl/nid.h>
#include <openssl/bn.h>
#include <openssl/obj_mac.h>


// Copied from: boringssl/crypto/fipsmodule/ec/ec_test.cc
static const uint8_t kECKeyWithoutPublic[] = {
  0x30, 0x31, 0x02, 0x01, 0x01, 0x04, 0x20, 0xc6, 0xc1, 0xaa, 0xda, 0x15, 0xb0,
  0x76, 0x61, 0xf8, 0x14, 0x2c, 0x6c, 0xaf, 0x0f, 0xdb, 0x24, 0x1a, 0xff, 0x2e,
  0xfe, 0x46, 0xc0, 0x93, 0x8b, 0x74, 0xf2, 0xbc, 0xc5, 0x30, 0x52, 0xb0, 0x77,
  0xa0, 0x0a, 0x06, 0x08, 0x2a, 0x86, 0x48, 0xce, 0x3d, 0x03, 0x01, 0x07,
};

TEST(EC_KEYTest, test_EC_KEY_parse_private_key) {
  CBS cbs;
  CBS_init(&cbs, kECKeyWithoutPublic, sizeof(kECKeyWithoutPublic));

  EC_KEY *key = EC_KEY_parse_private_key(&cbs, nullptr);
  ASSERT_TRUE(key);

  const EC_GROUP *group = EC_KEY_get0_group(key);
  ASSERT_TRUE(group);

  ASSERT_EQ(NID_X9_62_prime256v1, EC_GROUP_get_curve_name(group));

  const BIGNUM *order {EC_GROUP_get0_order(group)};
  ASSERT_TRUE(order);
  ASSERT_EQ(256, BN_num_bits(order));

  ASSERT_EQ(256, EC_GROUP_get_degree(group));

  EC_KEY_free(key);
}

TEST(EC_KEYTest, test_EC_KEY_new_by_curve_name) {
    EC_KEY            *myecc  = nullptr;

    /* ---------------------------------------------------------- *
     * Create a EC key sructure, setting the group type from NID  *
     * ---------------------------------------------------------- */
    myecc = EC_KEY_new_by_curve_name(NID_secp224r1);
    EXPECT_NE(myecc, nullptr);

    EC_KEY_free(myecc);
}

TEST(EC_KEYTest, test_EC_KEY_set_public_key_affine_coordinates) {
    CBS cbs;
    CBS_init(&cbs, kECKeyWithoutPublic, sizeof(kECKeyWithoutPublic));

    EC_KEY *key = EC_KEY_parse_private_key(&cbs, nullptr);
    ASSERT_TRUE(key);

    const EC_GROUP *group = EC_KEY_get0_group(key);
    ASSERT_TRUE(group);

    const BIGNUM *x {EC_GROUP_get0_order(group)};
    const BIGNUM *y {EC_GROUP_get0_order(group)};

    // TODO run the openssl test
    // check_ec_key_field_public_range_test
    // in test/ectest.c
    // at this point just test compiles
    EC_KEY_set_public_key_affine_coordinates(key, x, y);

    EC_KEY_free(key);
}