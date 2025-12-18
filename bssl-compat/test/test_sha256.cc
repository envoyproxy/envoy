#include <sstream>
#include <gtest/gtest.h>
#include <openssl/sha.h>

TEST(SHA256Test, test_SHA256) {

    std::string data = {
"The winters in the latitude of Sullivan’s Island are seldom very severe, and in the fall of the year it is a rare event indeed when a fire is considered necessary. About the middle of October, 18—, there occurred, however, a day of remarkable chilliness.\n"
    };

    std::string more = {
    "The Gold-Bug is a short story written by Edgar Allan Poe and published in 1843.\n"
    };

    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256_CTX ctx;
    int res = SHA256_Init(&ctx);
    EXPECT_EQ(res, 1);
    res = SHA256_Update(&ctx, data.c_str(), data.size());
    EXPECT_EQ(res, 1);
    res = SHA256_Update(&ctx, more.c_str(), more.size());
    EXPECT_EQ(res, 1);
    res = SHA256_Final(hash, &ctx);
    EXPECT_EQ(res, 1);
    std::stringstream ss;
    for (int i = 0; i < SHA256_DIGEST_LENGTH; i++) {
        ss << std::hex << std::setw(2) << std::setfill('0') << (int) hash[i];
    }
    // Refer to https://www.openssl.org/docs/man3.0/man1/openssl-dgst.html
    // openssl dgst -hex
    ASSERT_EQ(ss.str(), "517c7a7916ab9909104399f050daedbfe87d57f0543105cf9f00bd5426360a33");
}