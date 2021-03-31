#include "common/common/thread.h"

namespace Envoy {
namespace Thread {

bool MainThread::isMainThread() {
  // If threading is off, only main thread is running.
  auto main_thread_singleton = MainThreadSingleton::getExisting();
  if (main_thread_singleton == nullptr) {
    return true;
  }
  // When threading is on, compare thread id with main thread id.
  return main_thread_singleton->inMainThread() || main_thread_singleton->inTestThread();
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
