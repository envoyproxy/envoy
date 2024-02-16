#include "source/extensions/matching/string/lua/match.h"
#include "test/test_common/logging.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Matching {
namespace String {
namespace Lua {

namespace {
bool test(const std::string& code, const std::string& str) {
  LuaStringMatcher matcher(code);
  return matcher.match(str);
}

const std::string program = R"(
    -- Test that these locals are properly captured in the state.
    local good_val = "match"
    local bad_val = "nomatch"

    function match(str)
      if str == good_val then
        return true
      elseif str == bad_val then
        return false
      end
    end

    -- Test that no error is raised for this un-called code.
    function not_called(blah)
      panic("foo")
    end
  )";

const std::string no_match_function_program = R"(
    function wrong()
      return false
    end
  )";

const std::string invalid_lua_program = R"(
    if
  )";
} // namespace

TEST(LuaStringMatcher, LuaBehavior) {
  EXPECT_THROW_WITH_MESSAGE(test(no_match_function_program, ""), EnvoyException,
                            "Lua code did not contain a global function named 'match'");

  EXPECT_THROW_WITH_REGEX(
      test(invalid_lua_program, ""), EnvoyException,
      "Failed to load lua code in Lua StringMatcher:.*unexpected symbol near '<eof>'");

  EXPECT_TRUE(test(program, "match"));

  EXPECT_LOG_NOT_CONTAINS("error", "Lua StringMatcher",
                          { EXPECT_FALSE(test(program, "nomatch")); });

  EXPECT_LOG_CONTAINS("error", "function did not return a boolean",
                      { EXPECT_FALSE(test(program, "unknown")); });
}

// Ensure that the code runs in a context that the standard library is loaded into.
TEST(LuaStringMatcher, LuaStdLib) {
  const std::string code = R"(
    function match(str)
      -- Requires the string library to be present.
      return string.find(str, "text") ~= nil
    end
  )";

  EXPECT_TRUE(test(code, "contains text!"));
  EXPECT_FALSE(test(code, "nope"));
}

} // namespace Lua
} // namespace String
} // namespace Matching
} // namespace Extensions
} // namespace Envoy
