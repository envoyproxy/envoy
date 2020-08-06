#include "common/thread_local/thread_local_impl.h"

#include <algorithm>
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

  if (free_slot_indexes_.empty()) {
    SlotImplPtr slot(new SlotImpl(*this, slots_.size()));
    auto wrapper = std::make_unique<Bookkeeper>(*this, std::move(slot));
    slots_.push_back(wrapper->slot_.get());
    return wrapper;
  }
  const uint32_t idx = free_slot_indexes_.front();
  free_slot_indexes_.pop_front();
  ASSERT(idx < slots_.size());
  SlotImplPtr slot(new SlotImpl(*this, idx));
  slots_[idx] = slot.get();
  return std::make_unique<Bookkeeper>(*this, std::move(slot));
}

bool InstanceImpl::SlotImpl::currentThreadRegistered() {
  return thread_local_data_.data_.size() > index_;
}

void InstanceImpl::SlotImpl::runOnAllThreads(const UpdateCb& cb) {
  parent_.runOnAllThreads([this, cb]() { setThreadLocal(index_, cb(get())); });
}

void InstanceImpl::SlotImpl::runOnAllThreads(const UpdateCb& cb, Event::PostCb complete_cb) {
  parent_.runOnAllThreads([this, cb]() { setThreadLocal(index_, cb(get())); }, complete_cb);
}

ThreadLocalObjectSharedPtr InstanceImpl::SlotImpl::get() {
  ASSERT(currentThreadRegistered());
  return thread_local_data_.data_[index_];
}

InstanceImpl::Bookkeeper::Bookkeeper(InstanceImpl& parent, SlotImplPtr&& slot)
    : parent_(parent), slot_(std::move(slot)),
      ref_count_(/*not used.*/ nullptr,
                 [slot = slot_.get(), &parent = this->parent_](uint32_t* /* not used */) {
                   // On destruction, post a cleanup callback on main thread, this could happen on
                   // any thread.
                   parent.scheduleCleanup(slot);
                 }) {}

ThreadLocalObjectSharedPtr InstanceImpl::Bookkeeper::get() { return slot_->get(); }

void InstanceImpl::Bookkeeper::runOnAllThreads(const UpdateCb& cb, Event::PostCb complete_cb) {
  slot_->runOnAllThreads(
      [cb, ref_count = this->ref_count_](ThreadLocalObjectSharedPtr previous) {
        return cb(std::move(previous));
      },
      complete_cb);
}

void InstanceImpl::Bookkeeper::runOnAllThreads(const UpdateCb& cb) {
  slot_->runOnAllThreads([cb, ref_count = this->ref_count_](ThreadLocalObjectSharedPtr previous) {
    return cb(std::move(previous));
  });
}

bool InstanceImpl::Bookkeeper::currentThreadRegistered() {
  return slot_->currentThreadRegistered();
}

void InstanceImpl::Bookkeeper::runOnAllThreads(Event::PostCb cb) {
  // Use ref_count_ to bookkeep how many on-the-fly callback are out there.
  slot_->runOnAllThreads([cb, ref_count = this->ref_count_]() { cb(); });
}

void InstanceImpl::Bookkeeper::runOnAllThreads(Event::PostCb cb, Event::PostCb main_callback) {
  // Use ref_count_ to bookkeep how many on-the-fly callback are out there.
  slot_->runOnAllThreads([cb, main_callback, ref_count = this->ref_count_]() { cb(); },
                         main_callback);
}

void InstanceImpl::Bookkeeper::set(InitializeCb cb) {
  slot_->set([cb, ref_count = this->ref_count_](Event::Dispatcher& dispatcher)
                 -> ThreadLocalObjectSharedPtr { return cb(dispatcher); });
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

// Puts the slot into a deferred delete container, the slot will be destructed when its out-going
// callback reference count goes to 0.
void InstanceImpl::recycle(SlotImplPtr&& slot) {
  ASSERT(std::this_thread::get_id() == main_thread_id_);
  ASSERT(slot != nullptr);
  auto* slot_addr = slot.get();
  deferred_deletes_.insert({slot_addr, std::move(slot)});
}

// Called by the Bookkeeper ref_count destructor, the SlotImpl in the deferred deletes map can be
// destructed now.
void InstanceImpl::scheduleCleanup(SlotImpl* slot) {
  if (shutdown_) {
    // If server is shutting down, do nothing here.
    // The destruction of Bookkeeper has already transferred the SlotImpl to the deferred_deletes_
    // queue. No matter if this method is called from a Worker thread, the SlotImpl will be
    // destructed on main thread when InstanceImpl destructs.
    return;
  }
  if (std::this_thread::get_id() == main_thread_id_) {
    // If called from main thread, save a callback.
    ASSERT(deferred_deletes_.contains(slot));
    deferred_deletes_.erase(slot);
    return;
  }
  main_thread_dispatcher_->post([slot, this]() {
    ASSERT(deferred_deletes_.contains(slot));
    // The slot is guaranteed to be put into the deferred_deletes_ map by Bookkeeper destructor.
    deferred_deletes_.erase(slot);
  });
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
  ASSERT(std::find(free_slot_indexes_.begin(), free_slot_indexes_.end(), index) ==
             free_slot_indexes_.end(),
         fmt::format("slot index {} already in free slot set!", index));
  free_slot_indexes_.push_back(index);
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

void InstanceImpl::runOnAllThreads(Event::PostCb cb, Event::PostCb all_threads_complete_cb) {
  ASSERT(std::this_thread::get_id() == main_thread_id_);
  ASSERT(!shutdown_);
  // Handle main thread first so that when the last worker thread wins, we could just call the
  // all_threads_complete_cb method. Parallelism of main thread execution is being traded off
  // for programming simplicity here.
  cb();

  Event::PostCbSharedPtr cb_guard(new Event::PostCb(cb),
                                  [this, all_threads_complete_cb](Event::PostCb* cb) {
                                    main_thread_dispatcher_->post(all_threads_complete_cb);
                                    delete cb;
                                  });

  for (Event::Dispatcher& dispatcher : registered_threads_) {
    dispatcher.post([cb_guard]() -> void { (*cb_guard)(); });
  }
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
