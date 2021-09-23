#include "source/common/common/thread.h"

namespace Envoy {
namespace Thread {

bool MainThread::isMainThread() {
  auto main_thread_singleton = MainThreadSingleton::getExisting();
  ASSERT(main_thread_singleton != nullptr);
  return main_thread_singleton->inMainThread() || main_thread_singleton->inTestThread();
}

void MainThread::clearSingleton() {
  delete MainThreadSingleton::getExisting();
  MainThreadSingleton::clear();
}

void MainThread::clearMainThread() {
  auto main_thread_singleton = MainThreadSingleton::getExisting();
  ASSERT(main_thread_singleton != nullptr);
  main_thread_singleton->main_thread_id_ = absl::nullopt;
}

/*void MainThread::initTestThread() {
  if (!initialized()) {
    MainThreadSingleton::initialize(new MainThread());
  }
  MainThreadSingleton::get().registerTestThread();
  }*/

void MainThread::initMainThread() {
  if (!initialized()) {
    MainThreadSingleton::initialize(new MainThread());
  }
  MainThreadSingleton::get().registerMainThread();
}

TestThread::TestThread() {
  auto main_thread_singleton = MainThread::MainThreadSingleton::getExisting();
  if (main_thread_singleton == nullptr) {
    main_thread_singleton = new MainThread();
    MainThread::MainThreadSingleton::initialize(main_thread_singleton);
    main_thread_singleton->registerTestThread();
  } else {
    main_thread_singleton->incRefCount();
    ASSERT(main_thread_singleton->inTestThread());
  }
}

TestThread::~TestThread() {
  auto main_thread_singleton = MainThread::MainThreadSingleton::getExisting();
  ASSERT(main_thread_singleton != nullptr);
  main_thread_singleton->decRefCount();
}

void MainThread::incRefCount() { ++ref_count_; }
void MainThread::decRefCount() {
  if (--ref_count_ == 0) {
    delete this;
  }
}

} // namespace Thread
} // namespace Envoy
