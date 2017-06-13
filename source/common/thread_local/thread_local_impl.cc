#include "common/thread_local/thread_local_impl.h"

#include <atomic>
#include <cstdint>
#include <list>

#include "envoy/event/dispatcher.h"

#include "common/common/assert.h"
#include "common/common/stl_helpers.h"

namespace Envoy {
namespace ThreadLocal {

std::atomic<uint32_t> InstanceImpl::next_slot_id_;
thread_local InstanceImpl::ThreadLocalData InstanceImpl::thread_local_data_;
std::list<std::reference_wrapper<Event::Dispatcher>> InstanceImpl::registered_threads_;

InstanceImpl::~InstanceImpl() { reset(); }

ThreadLocalObjectSharedPtr InstanceImpl::get(uint32_t index) {
  ASSERT(thread_local_data_.data_.size() > index);
  return thread_local_data_.data_[index];
}

void InstanceImpl::registerThread(Event::Dispatcher& dispatcher, bool main_thread) {
  ASSERT(std::this_thread::get_id() == main_thread_id_);

  if (main_thread) {
    main_thread_dispatcher_ = &dispatcher;
  } else {
    ASSERT(!containsReference(registered_threads_, dispatcher));
    registered_threads_.push_back(dispatcher);
  }
}

void InstanceImpl::runOnAllThreads(Event::PostCb cb) {
  ASSERT(std::this_thread::get_id() == main_thread_id_);
  for (Event::Dispatcher& dispatcher : registered_threads_) {
    dispatcher.post(cb);
  }

  // Handle main thread.
  cb();
}

void InstanceImpl::set(uint32_t index, InitializeCb cb) {
  ASSERT(std::this_thread::get_id() == main_thread_id_);
  for (Event::Dispatcher& dispatcher : registered_threads_) {
    dispatcher.post([index, cb, &dispatcher]() -> void { setThreadLocal(index, cb(dispatcher)); });
  }

  // Handle main thread.
  setThreadLocal(index, cb(*main_thread_dispatcher_));
}

void InstanceImpl::setThreadLocal(uint32_t index, ThreadLocalObjectSharedPtr object) {
  if (thread_local_data_.data_.size() <= index) {
    thread_local_data_.data_.resize(index + 1);
  }

  thread_local_data_.data_[index] = std::move(object);
}

void InstanceImpl::shutdownThread() {
  for (auto& entry : thread_local_data_.data_) {
    entry->shutdown();
  }
}

void InstanceImpl::reset() {
  ASSERT(std::this_thread::get_id() == main_thread_id_);
  next_slot_id_ = 0;
  thread_local_data_.data_.clear();
  registered_threads_.clear();
}

} // ThreadLocal
} // Envoy
