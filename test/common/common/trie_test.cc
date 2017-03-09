#include <vector>

#include "common/common/trie.h"

namespace {

class DomainTokenizer {
public:
  std::vector<std::string> tokenize(std::string str) const {
    std::vector<std::string> tokens;
    while (str.size() > 0) {
      size_t next = std::min(str.find('-'), str.find('.'));
      if (next == std::string::npos) {
        tokens.push_back(str);
        str.clear();
        break;
      }
      if (next > 0) {
        tokens.push_back(str.substr(0, next));
      }
      tokens.push_back(str.substr(next, 1));
      str.erase(0, next + 1);
    }
    std::reverse(tokens.begin(), tokens.end());
    return tokens;
  }
};

TEST(TrieTest, BasicFunctionality) {
  TrieNode<std::string, std::string*, DomainTokenizer> root(nullptr);

  std::string v1("This is the value");
  root.insert(".bar.com", &v1);
  auto vp = root.match("baz.foo.bar.com");
  ASSERT_NE(nullptr, vp.first);
  EXPECT_STREQ("This is the value", vp.first->c_str());
  EXPECT_FALSE(vp.second);

  vp = root.match("baz.foo.bat.com");
  EXPECT_EQ(nullptr, vp.first);
  EXPECT_FALSE(vp.second);

  std::string v2("This is the other value");
  root.insert(".foo.bat.com", &v2);
  vp = root.match("baz.foo.bat.com");
  EXPECT_STREQ("This is the other value", vp.first->c_str());
  EXPECT_FALSE(vp.second);

  std::string v3("This is a third value");
  root.insert(".bat.com", &v3);
  vp = root.match("baz.foo.bat.com");
  EXPECT_STREQ("This is the other value", vp.first->c_str());
  EXPECT_FALSE(vp.second);
  vp = root.match("foo.baz.bat.com");
  EXPECT_STREQ("This is a third value", vp.first->c_str());
  EXPECT_FALSE(vp.second);

  std::string v4("This is #4");
  root.insert("-foo.bat.com", &v4);
  vp = root.match("baz.foo.bat.com");
  EXPECT_STREQ("This is the other value", vp.first->c_str());
  EXPECT_FALSE(vp.second);
  vp = root.match("foo.baz.bat.com");
  EXPECT_STREQ("This is a third value", vp.first->c_str());
  EXPECT_FALSE(vp.second);
  vp = root.match("abc-foo.bat.com");
  EXPECT_STREQ("This is #4", vp.first->c_str());
  EXPECT_FALSE(vp.second);

  std::string v5("This is the fifth");
  std::string v6("This is the sixth");
  root.insert("foo.bar.baz.bat", &v5);
  root.insert("xyz.foo.bar.baz.bat", &v6);
  vp = root.match("foo.bar.baz.bat");
  ASSERT_NE(nullptr, vp.first);
  EXPECT_STREQ("This is the fifth", vp.first->c_str());
  EXPECT_TRUE(vp.second);
  vp = root.match("xyz.foo.bar.baz.bat");
  ASSERT_NE(nullptr, vp.first);
  EXPECT_STREQ("This is the sixth", vp.first->c_str());
  EXPECT_TRUE(vp.second);
}

} // namespace
