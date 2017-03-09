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
  std::string* vp = root.match("baz.foo.bar.com");
  ASSERT_NE(nullptr, vp);
  EXPECT_STREQ("This is the value", vp->c_str());

  vp = root.match("baz.foo.bat.com");
  EXPECT_EQ(nullptr, vp);

  std::string v2("This is the other value");
  root.insert(".foo.bat.com", &v2);
  vp = root.match("baz.foo.bat.com");
  EXPECT_STREQ("This is the other value", vp->c_str());

  std::string v3("This is a third value");
  root.insert(".bat.com", &v3);
  vp = root.match("baz.foo.bat.com");
  EXPECT_STREQ("This is the other value", vp->c_str());
  vp = root.match("foo.baz.bat.com");
  EXPECT_STREQ("This is a third value", vp->c_str());

  std::string v4("This is #4");
  root.insert("-foo.bat.com", &v4);
  vp = root.match("baz.foo.bat.com");
  EXPECT_STREQ("This is the other value", vp->c_str());
  vp = root.match("foo.baz.bat.com");
  EXPECT_STREQ("This is a third value", vp->c_str());
  vp = root.match("abc-foo.bat.com");
  EXPECT_STREQ("This is #4", vp->c_str());
}

} // namespace
