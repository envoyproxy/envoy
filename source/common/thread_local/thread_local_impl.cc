#include "common/thread_local/thread_local_impl.h"

#include <atomic>
#include <cstdint>
#include <list>

#include "envoy/event/dispatcher.h"

#include "common/common/assert.h"
#include "common/common/stl_helpers.h"

namespace Envoy {
namespace ThreadLocal {

thread_local InstanceImpl::ThreadLocalData InstanceImpl::thread_local_data_;

InstanceImpl::~InstanceImpl() {
  ASSERT(std::this_thread::get_id() == main_thread_id_);
  ASSERT(shutdown_);
  thread_local_data_.data_.clear();
}

SlotPtr InstanceImpl::allocateSlot() {
  ASSERT(std::this_thread::get_id() == main_thread_id_);
  ASSERT(!shutdown_);

  for (uint64_t i = 0; i < slots_.size(); i++) {
    if (slots_[i] == nullptr) {
      std::unique_ptr<SlotImpl> slot(new SlotImpl(*this, i));
      slots_[i] = slot.get();
      return std::move(slot);
    }
  }

  std::unique_ptr<SlotImpl> slot(new SlotImpl(*this, slots_.size()));
  slots_.push_back(slot.get());
  return std::move(slot);
}

ThreadLocalObjectSharedPtr InstanceImpl::SlotImpl::get() {
  ASSERT(thread_local_data_.data_.size() > index_);
  return thread_local_data_.data_[index_];
}

void InstanceImpl::registerThread(Event::Dispatcher& dispatcher, bool main_thread) {
  ASSERT(std::this_thread::get_id() == main_thread_id_);
  ASSERT(!shutdown_);

  if (main_thread) {
    main_thread_dispatcher_ = &dispatcher;
    thread_local_data_.dispatcher_ = &dispatcher;
  } else {
    ASSERT(!containsReference(registered_threads_, dispatcher));
    registered_threads_.push_back(dispatcher);
    dispatcher.post([&dispatcher] { thread_local_data_.dispatcher_ = &dispatcher; });
  }
}

void InstanceImpl::removeSlot(SlotImpl& slot) {
  ASSERT(std::this_thread::get_id() == main_thread_id_);

  // When shutting down, we do not post slot removals to other threads. This is because the other
  // threads have already shut down and the dispatcher is no longer alive. There is also no reason
  // to do removal, because no allocations happen during shutdown and shutdownThread() will clean
  // things up on the other thread.
  if (shutdown_) {
    return;
  }

  const uint64_t index = slot.index_;
  slots_[index] = nullptr;
  runOnAllThreads([index]() -> void {
    // This runs on each thread and clears the slot, making it available for a new allocations.
    // This is safe even if a new allocation comes in, because everything happens with post() and
    // will be sequenced after this removal.
    if (index < thread_local_data_.data_.size()) {
      thread_local_data_.data_[index] = nullptr;
    }
  });
}

void InstanceImpl::runOnAllThreads(Event::PostCb cb) {
  ASSERT(std::this_thread::get_id() == main_thread_id_);
  ASSERT(!shutdown_);

  for (Event::Dispatcher& dispatcher : registered_threads_) {
    dispatcher.post(cb);
  }

  // Handle main thread.
  cb();
}

void InstanceImpl::SlotImpl::set(InitializeCb cb) {
  ASSERT(std::this_thread::get_id() == parent_.main_thread_id_);
  ASSERT(!parent_.shutdown_);

  for (Event::Dispatcher& dispatcher : parent_.registered_threads_) {
    const uint32_t index = index_;
    dispatcher.post([index, cb, &dispatcher]() -> void { setThreadLocal(index, cb(dispatcher)); });
  }

  // Handle main thread.
  setThreadLocal(index_, cb(*parent_.main_thread_dispatcher_));
}

void InstanceImpl::setThreadLocal(uint32_t index, ThreadLocalObjectSharedPtr object) {
  if (thread_local_data_.data_.size() <= index) {
    thread_local_data_.data_.resize(index + 1);
  }

  thread_local_data_.data_[index] = object;
}

void InstanceImpl::shutdownGlobalThreading() {
  ASSERT(std::this_thread::get_id() == main_thread_id_);
  ASSERT(!shutdown_);
  shutdown_ = true;
}

void InstanceImpl::shutdownThread() {
  ASSERT(shutdown_);

  // Destruction of slots is done in *reverse* order. This is so that filters and higher layer
  // things that are built on top of the cluster manager, stats, etc. will be destroyed before
  // more base layer things. The reason reverse ordering is done is to deal with the case that leaf
  // objects depend in some way on "persistent" objects (particularly the cluster manager) that are
  // created very early on with a known slot number and never destroyed until shutdown. For example,
  // if we chose to create persistent per-thread gRPC clients we would potentially run into shutdown
  // issues if that thing got destroyed after the cluster manager. This happens in practice
  // currently when a redis connection pool is destroyed and removes its member update callback from
  // the backing cluster. Examples of things with TLS that are created early on and are never
  // destroyed until server shutdown are stats, runtime, and the cluster manager (see server.cc).
  //
  // It's possible this might need to become more complicated later but it's OK for now. Note that
  // this is always safe to do because:
  // 1) All slot updates come in via post().
  // 2) No updates or removals will come in during shutdown().
  //
  // TODO(mattklein123): Deletion should really be in reverse *allocation* order. This could be
  //                     implemented relatively easily by keeping a parallel list of slot #s. This
  //                     would fix the case where something allocates two slots, but is interleaved
  //                     with a deletion, such that the second allocation is actually a lower slot
  //                     number than the first. This is an edge case that does not exist anywhere
  //                     in the code today, but we can keep this in mind if things become more
  //                     complicated in the future.
  for (auto it = thread_local_data_.data_.rbegin(); it != thread_local_data_.data_.rend(); ++it) {
    it->reset();
  }
  thread_local_data_.data_.clear();
}

Event::Dispatcher& InstanceImpl::dispatcher() {
  ASSERT(thread_local_data_.dispatcher_ != nullptr);
  return *thread_local_data_.dispatcher_;
}

} // namespace ThreadLocal
} // namespace Envoy
