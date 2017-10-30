#include "common/buffer/buffer_impl.h"
#include "common/lua/wrappers.h"

#include "test/test_common/lua_wrappers.h"
#include "test/test_common/utility.h"

namespace Envoy {
namespace Lua {

class LuaBufferWrapperTest : public LuaWrappersTestBase<BufferWrapper> {};

// Basic buffer wrapper methods test.
TEST_F(LuaBufferWrapperTest, Methods) {
  const std::string SCRIPT{R"EOF(
    function callMe(object)
      testPrint(object:length())
      testPrint(object:getBytes(0, 2))
      testPrint(object:getBytes(6, 5))
    end
  )EOF"};

  setup(SCRIPT);
  Buffer::OwnedImpl data("hello world");
  BufferWrapper::create(coroutine_->luaState(), data);
  EXPECT_CALL(*this, testPrint("11"));
  EXPECT_CALL(*this, testPrint("he"));
  EXPECT_CALL(*this, testPrint("world"));
  start("callMe");
}

// Invalid params for the buffer wrapper getBytes() call.
TEST_F(LuaBufferWrapperTest, GetBytesInvalidParams) {
  const std::string SCRIPT{R"EOF(
    function callMe(object)
      object:getBytes(100, 100)
    end
  )EOF"};

  setup(SCRIPT);
  Buffer::OwnedImpl data("hello world");
  BufferWrapper::create(coroutine_->luaState(), data);
  EXPECT_THROW_WITH_MESSAGE(
      start("callMe"), LuaException,
      "[string \"...\"]:3: index/length must be >= 0 and (index + length) must be <= buffer size");
}

} // namespace Lua
} // namespace Envoy
