#include "common/common/thread.h"

namespace Envoy {
namespace Thread {

bool MainThread::isMainThread() {
  // If threading is off, only main thread is running.
  if (MainThreadSingleton::getExisting() == nullptr) {
    return true;
  }
  // When threading is on, compare thread id with main thread id.
  return MainThreadSingleton::get().inMainThread() || MainThreadSingleton::get().inTestThread();
}

void MainThread::clear() {
  delete MainThreadSingleton::getExisting();
  MainThreadSingleton::clear();
}

void MainThread::initTestThread() {
  if (!initialized()) {
    MainThreadSingleton::initialize(new MainThread());
  }
  MainThreadSingleton::get().registerTestThread();
}

void MainThread::initMainThread() {
  if (!initialized()) {
    MainThreadSingleton::initialize(new MainThread());
  }
  MainThreadSingleton::get().registerMainThread();
}

} // namespace Thread
} // namespace Envoy
