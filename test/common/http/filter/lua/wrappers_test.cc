#include "common/http/filter/lua/wrappers.h"

#include "test/test_common/lua_wrappers.h"
#include "test/test_common/utility.h"

using testing::InSequence;

namespace Envoy {
namespace Http {
namespace Filter {
namespace Lua {

class LuaHeaderMapWrapperTest : public Envoy::Lua::LuaWrappersTestBase<HeaderMapWrapper> {
public:
  virtual void setup(const std::string& script) {
    Envoy::Lua::LuaWrappersTestBase<HeaderMapWrapper>::setup(script);
    state_->registerType<HeaderMapIterator>();
  }
};

class LuaMetadataMapWrapperTest : public Envoy::Lua::LuaWrappersTestBase<MetadataMapWrapper> {
public:
  virtual void setup(const std::string& script) {
    Envoy::Lua::LuaWrappersTestBase<MetadataMapWrapper>::setup(script);
  }

  envoy::api::v2::core::Metadata parseMetadataFromYaml(const std::string& yaml_string) {
    envoy::api::v2::core::Metadata metadata;
    MessageUtil::loadFromYaml(yaml_string, metadata);
    return metadata;
  }
};

// Basic methods test for the metadata wrapper.
TEST_F(LuaMetadataMapWrapperTest, Methods) {
  const std::string SCRIPT{R"EOF(
    function callMe(object)
      recipe = object:get("make.delicious.bread")

      testPrint(recipe["name"])
      testPrint(recipe["origin"])

      testPrint(tostring(recipe["lactose"]))
      testPrint(tostring(recipe["nut"]))

      testPrint(tostring(recipe["portion"]))
      testPrint(tostring(recipe["minutes"]))

      testPrint(recipe["butter"]["type"])
      testPrint(tostring(recipe["butter"]["expensive"]))

      for i, ingredient in ipairs(recipe["ingredients"]) do
        testPrint(ingredient)
      end
    end
  )EOF"};

  InSequence s;
  setup(SCRIPT);

  const std::string yaml = R"EOF(
filter_metadata:
  make.delicious.bread:
    name: pulla
    origin: finland
    lactose: true
    nut: false
    portion: 5
    minutes: 30.5
    butter:
      type: grass_fed
      expensive: false
    ingredients:
      - fluor
      - milk
)EOF";

  envoy::api::v2::core::Metadata metadata = parseMetadataFromYaml(yaml);
  MetadataMapWrapper::create(coroutine_->luaState(), metadata);

  EXPECT_CALL(*this, testPrint("pulla"));
  EXPECT_CALL(*this, testPrint("finland"));

  EXPECT_CALL(*this, testPrint("true"));
  EXPECT_CALL(*this, testPrint("false"));

  EXPECT_CALL(*this, testPrint("5"));
  EXPECT_CALL(*this, testPrint("30.5"));

  EXPECT_CALL(*this, testPrint("grass_fed"));
  EXPECT_CALL(*this, testPrint("false"));

  EXPECT_CALL(*this, testPrint("fluor"));
  EXPECT_CALL(*this, testPrint("milk"));

  start("callMe");
}

// Basic methods test for the header wrapper.
TEST_F(LuaHeaderMapWrapperTest, Methods) {
  const std::string SCRIPT{R"EOF(
    function callMe(object)
      object:add("HELLO", "WORLD")
      testPrint(object:get("hELLo"))

      object:add("header1", "")
      object:add("header2", "foo")

      for key, value in pairs(object) do
        testPrint(string.format("'%s' '%s'", key, value))
      end

      object:remove("header1")
      for key, value in pairs(object) do
        testPrint(string.format("'%s' '%s'", key, value))
      end
    end
  )EOF"};

  InSequence s;
  setup(SCRIPT);

  TestHeaderMapImpl headers;
  HeaderMapWrapper::create(coroutine_->luaState(), headers, []() { return true; });
  EXPECT_CALL(*this, testPrint("WORLD"));
  EXPECT_CALL(*this, testPrint("'hello' 'WORLD'"));
  EXPECT_CALL(*this, testPrint("'header1' ''"));
  EXPECT_CALL(*this, testPrint("'header2' 'foo'"));
  EXPECT_CALL(*this, testPrint("'hello' 'WORLD'"));
  EXPECT_CALL(*this, testPrint("'header2' 'foo'"));
  start("callMe");
}

// Test modifiable methods.
TEST_F(LuaHeaderMapWrapperTest, ModifiableMethods) {
  const std::string SCRIPT{R"EOF(
    function shouldBeOk(object)
      object:get("hELLo")
      for key, value in pairs(object) do
      end
    end

    function shouldFailRemove(object)
      object:remove("foo")
    end

    function shouldFailAdd(object)
      object:add("foo")
    end

    function shouldFailReplace(object)
      object:replace("foo")
    end
  )EOF"};

  InSequence s;
  setup(SCRIPT);

  TestHeaderMapImpl headers;
  HeaderMapWrapper::create(coroutine_->luaState(), headers, []() { return false; });
  start("shouldBeOk");

  setup(SCRIPT);
  HeaderMapWrapper::create(coroutine_->luaState(), headers, []() { return false; });
  EXPECT_THROW_WITH_MESSAGE(start("shouldFailRemove"), Envoy::Lua::LuaException,
                            "[string \"...\"]:9: header map can no longer be modified");

  setup(SCRIPT);
  HeaderMapWrapper::create(coroutine_->luaState(), headers, []() { return false; });
  EXPECT_THROW_WITH_MESSAGE(start("shouldFailAdd"), Envoy::Lua::LuaException,
                            "[string \"...\"]:13: header map can no longer be modified");

  setup(SCRIPT);
  HeaderMapWrapper::create(coroutine_->luaState(), headers, []() { return false; });
  EXPECT_THROW_WITH_MESSAGE(start("shouldFailReplace"), Envoy::Lua::LuaException,
                            "[string \"...\"]:17: header map can no longer be modified");
}

// Verify that replace works correctly with both inline and normal headers.
TEST_F(LuaHeaderMapWrapperTest, Replace) {
  const std::string SCRIPT{R"EOF(
    function callMe(object)
      object:replace(":path", "/new_path")
      object:replace("other_header", "other_header_value")
      object:replace("new_header", "new_header_value")
    end
  )EOF"};

  InSequence s;
  setup(SCRIPT);

  TestHeaderMapImpl headers{{":path", "/"}, {"other_header", "hello"}};
  HeaderMapWrapper::create(coroutine_->luaState(), headers, []() { return true; });
  start("callMe");

  EXPECT_EQ((TestHeaderMapImpl{{":path", "/new_path"},
                               {"other_header", "other_header_value"},
                               {"new_header", "new_header_value"}}),
            headers);
}

// Modify during iteration.
TEST_F(LuaHeaderMapWrapperTest, ModifyDuringIteration) {
  const std::string SCRIPT{R"EOF(
    function callMe(object)
      for key, value in pairs(object) do
        object:add("hello", "world")
      end
    end
  )EOF"};

  InSequence s;
  setup(SCRIPT);

  TestHeaderMapImpl headers{{"foo", "bar"}};
  HeaderMapWrapper::create(coroutine_->luaState(), headers, []() { return true; });
  EXPECT_THROW_WITH_MESSAGE(start("callMe"), Envoy::Lua::LuaException,
                            "[string \"...\"]:4: header map cannot be modified while iterating");
}

// Modify after iteration.
TEST_F(LuaHeaderMapWrapperTest, ModifyAfterIteration) {
  const std::string SCRIPT{R"EOF(
    function callMe(object)
      for key, value in pairs(object) do
        testPrint(string.format("'%s' '%s'", key, value))
      end

      object:add("hello", "world")

      for key, value in pairs(object) do
        testPrint(string.format("'%s' '%s'", key, value))
      end
    end
  )EOF"};

  InSequence s;
  setup(SCRIPT);

  TestHeaderMapImpl headers{{"foo", "bar"}};
  HeaderMapWrapper::create(coroutine_->luaState(), headers, []() { return true; });
  EXPECT_CALL(*this, testPrint("'foo' 'bar'"));
  EXPECT_CALL(*this, testPrint("'foo' 'bar'"));
  EXPECT_CALL(*this, testPrint("'hello' 'world'"));
  start("callMe");
}

// Don't finish iteration.
TEST_F(LuaHeaderMapWrapperTest, DontFinishIteration) {
  const std::string SCRIPT{R"EOF(
    function callMe(object)
      iterator = pairs(object)
      key, value = iterator()
      iterator2 = pairs(object)
    end
  )EOF"};

  InSequence s;
  setup(SCRIPT);

  TestHeaderMapImpl headers{{"foo", "bar"}, {"hello", "world"}};
  HeaderMapWrapper::create(coroutine_->luaState(), headers, []() { return true; });
  EXPECT_THROW_WITH_MESSAGE(
      start("callMe"), Envoy::Lua::LuaException,
      "[string \"...\"]:5: cannot create a second iterator before completing the first");
}

// Use iterator across yield.
TEST_F(LuaHeaderMapWrapperTest, IteratorAcrossYield) {
  const std::string SCRIPT{R"EOF(
    function callMe(object)
      iterator = pairs(object)
      coroutine.yield()
      iterator()
    end
  )EOF"};

  InSequence s;
  setup(SCRIPT);

  TestHeaderMapImpl headers{{"foo", "bar"}, {"hello", "world"}};
  Envoy::Lua::LuaDeathRef<HeaderMapWrapper> wrapper(
      HeaderMapWrapper::create(coroutine_->luaState(), headers, []() { return true; }), true);
  yield_callback_ = [] {};
  start("callMe");
  wrapper.reset();
  EXPECT_THROW_WITH_MESSAGE(coroutine_->resume(0, [] {}), Envoy::Lua::LuaException,
                            "[string \"...\"]:5: object used outside of proper scope");
}

} // namespace Lua
} // namespace Filter
} // namespace Http
} // namespace Envoy
