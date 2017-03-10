#include "common/common/trie.h"
#include "common/common/utility.h"

namespace {

struct ReverseDomainTokenizer {
  std::vector<std::string> tokenize(const std::string& str) const {
    std::vector<std::string> tokens(StringUtil::tokenize(str, "-."));
    std::reverse(tokens.begin(), tokens.end());
    return tokens;
  }
};

TEST(TrieTest, DomainName) {
  TrieNode<std::string, std::string*, ReverseDomainTokenizer> root;

  // Test basic lookup
  std::string v1("This is the value");
  root.emplace(".bar.com", &v1);
  auto vp = root.find("baz.foo.bar.com");
  ASSERT_NE(nullptr, vp.first);
  EXPECT_STREQ("This is the value", vp.first->c_str());
  EXPECT_FALSE(vp.second);

  // Test basic lookup failure.
  vp = root.find("baz.foo.bat.com");
  EXPECT_EQ(nullptr, vp.first);
  EXPECT_FALSE(vp.second);

  // Make sure a second path works.
  std::string v2("This is the other value");
  root.emplace(".foo.bat.com", &v2);
  vp = root.find("baz.foo.bat.com");
  EXPECT_STREQ("This is the other value", vp.first->c_str());
  EXPECT_FALSE(vp.second);

  // Make sure that more specific paths take precedence. (i.e. baz.foo.bat.com
  // matches .foo.bat.com not the less specific .bat.com.)
  std::string v3("This is a third value");
  root.emplace(".bat.com", &v3);
  vp = root.find("baz.foo.bat.com");
  EXPECT_STREQ("This is the other value", vp.first->c_str());
  EXPECT_FALSE(vp.second);
  vp = root.find("foo.baz.bat.com");
  EXPECT_STREQ("This is a third value", vp.first->c_str());
  EXPECT_FALSE(vp.second);

  // Different token boundaries should be distinct matches ('-' v. '.')
  std::string v4("This is #4");
  root.emplace("-foo.bat.com", &v4);
  vp = root.find("baz.foo.bat.com");
  EXPECT_STREQ("This is the other value", vp.first->c_str());
  EXPECT_FALSE(vp.second);
  vp = root.find("foo.baz.bat.com");
  EXPECT_STREQ("This is a third value", vp.first->c_str());
  EXPECT_FALSE(vp.second);
  vp = root.find("abc-foo.bat.com");
  EXPECT_STREQ("This is #4", vp.first->c_str());
  EXPECT_FALSE(vp.second);

  // Exact matches should report as such.
  std::string v5("This is the fifth");
  std::string v6("This is the sixth");
  root.emplace("foo.bar.baz.bat", &v5);
  root.emplace("xyz.foo.bar.baz.bat", &v6);
  vp = root.find("foo.bar.baz.bat");
  EXPECT_STREQ("This is the fifth", vp.first->c_str());
  EXPECT_TRUE(vp.second);
  vp = root.find("xyz.foo.bar.baz.bat");
  ASSERT_NE(nullptr, vp.first);
  EXPECT_STREQ("This is the sixth", vp.first->c_str());
  EXPECT_TRUE(vp.second);

  // Edge cases
  std::string v7("This is a single node trie");
  root.emplace(".moo", &v7);
  vp = root.find("alt.cows.moo.moo.moo");
  EXPECT_STREQ("This is a single node trie", vp.first->c_str());
  EXPECT_FALSE(vp.second);
  std::string v8("This is another single node trie");
  root.emplace(".", &v8);
  vp = root.find(".");
  EXPECT_STREQ("This is another single node trie", vp.first->c_str());
  EXPECT_TRUE(vp.second);
  vp = root.find("moo.");
  EXPECT_STREQ("This is another single node trie", vp.first->c_str());
  EXPECT_FALSE(vp.second);
  vp = root.find("");
  EXPECT_EQ(nullptr, vp.first);
  EXPECT_FALSE(vp.second);
  std::string v9("Dave's not here!");
  root.emplace("", &v9);
  vp = root.find("horse");
  EXPECT_EQ(nullptr, vp.first);
  EXPECT_FALSE(vp.second);
  vp = root.find("");
  EXPECT_EQ(nullptr, vp.first);
  EXPECT_FALSE(vp.second);
}

class Uint64OctetTokenizer {
public:
  std::vector<uint64_t> tokenize(uint64_t num) const {
    std::vector<uint64_t> octets{num >> 56 & 0xFF, num >> 48 & 0xFF, num >> 40 & 0xFF,
                                 num >> 32 & 0xFF, num >> 24 & 0xFF, num >> 16 & 0xFF,
                                 num >> 8 & 0xFF,  num & 0xFF};

    // Trim trailing zeros
    while (!octets.empty() && octets.back() == 0) {
      octets.pop_back();
    }
    return octets;
  }
};

TEST(TrieTest, Uint64) {
  TrieNode<uint64_t, std::string*, Uint64OctetTokenizer> root;

  // Basic recall
  std::string v1("String 1");
  root.emplace(0x0102030400000000, &v1);
  auto vp = root.find(0x0102030405060708);
  EXPECT_STREQ("String 1", vp.first->c_str());
  EXPECT_FALSE(vp.second);
  vp = root.find(0x0102030400000000);
  EXPECT_STREQ("String 1", vp.first->c_str());
  EXPECT_TRUE(vp.second);

  // Lookup failure.
  vp = root.find(0x0000000005060708);
  EXPECT_EQ(nullptr, vp.first);
  EXPECT_FALSE(vp.second);
  vp = root.find(0x0102000000000000);
  EXPECT_EQ(nullptr, vp.first);
  EXPECT_FALSE(vp.second);

  // Divergent paths
  std::string v2("String 2");
  root.emplace(0x0102030500000000, &v2);
  vp = root.find(0x0102030500000000);
  EXPECT_STREQ("String 2", vp.first->c_str());
  EXPECT_TRUE(vp.second);
  vp = root.find(0x01020305070B0D11);
  EXPECT_STREQ("String 2", vp.first->c_str());
  EXPECT_FALSE(vp.second);
  vp = root.find(0x0102000000000000);
  EXPECT_EQ(nullptr, vp.first);
  EXPECT_FALSE(vp.second);

  // Empty key
  std::string v3("String 3");
  root.emplace(0, &v3);
  vp = root.find(0);
  EXPECT_EQ(nullptr, vp.first);
  EXPECT_FALSE(vp.second);
}

} // namespace
