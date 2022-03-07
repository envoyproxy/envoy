#pragma once

#include <functional>

namespace Envoy {
namespace Extensions {
namespace Common {
namespace AsyncFiles {

class AsyncFileContextImpl;

// Actions to be passed to asyncFileHandle->enqueue.
//
// Action callbacks run in the AsyncFileManager's thread pool, and therefore:
// 1. Must not reference variables on the stack from the scope of the caller.
// 2. Must not block significantly or do significant work - if anything time-consuming is required
// the result
//    should be passed to another thread for handling.
class AsyncFileAction {
public:
  virtual ~AsyncFileAction() = default;

private:
  virtual void execute(AsyncFileContextImpl* context) = 0;
  virtual void callback() = 0;
  friend class AsyncFileContextImpl;
};

} // namespace AsyncFiles
} // namespace Common
} // namespace Extensions
} // namespace Envoy
