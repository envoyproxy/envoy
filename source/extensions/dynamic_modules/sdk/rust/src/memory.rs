//! Tracking of the heap memory allocated by a dynamic module.
//!
//! [`declare_tracking_allocator!`](crate::declare_tracking_allocator) installs a
//! [`TrackingAllocator`] as the global allocator of the module and reports the bytes it counts to
//! Envoy, so that the dynamic module memory resource monitor uses them instead of an estimate.

use std::alloc::{GlobalAlloc, Layout, System};
use std::cell::Cell;
use std::sync::atomic::{AtomicBool, AtomicI64, Ordering};

/// The number of threads that count their allocations in a slot of their own. A thread that finds
/// every slot taken counts in the pool instead.
const THREAD_SLOTS: usize = 256;
const UNASSIGNED_SLOT: usize = usize::MAX;
const NO_SLOT: usize = usize::MAX - 1;

/// The credit that a slot reserves from the pool at once.
const CREDIT_BATCH: i64 = 64 << 10;

/// A counter on a cache line of its own, so that threads counting in different slots never contend.
#[repr(align(128))]
struct Counter(AtomicI64);

impl Counter {
  const fn new() -> Self {
    Self(AtomicI64::new(0))
  }
}

static SLOT_IN_USE: [AtomicBool; THREAD_SLOTS] = [const { AtomicBool::new(false) }; THREAD_SLOTS];

thread_local! {
  // Has no destructor, so that reading it never registers one.
  static THREAD_SLOT: Cell<usize> = const { Cell::new(UNASSIGNED_SLOT) };
  static THREAD_SLOT_RELEASER: ThreadSlotReleaser = const { ThreadSlotReleaser };
}

/// Returns the slot owned by the calling thread, or [`NO_SLOT`].
#[inline(always)]
fn thread_slot() -> usize {
  let slot = THREAD_SLOT.with(Cell::get);
  if slot != UNASSIGNED_SLOT {
    return slot;
  }
  acquire_thread_slot()
}

#[cold]
#[inline(never)]
fn acquire_thread_slot() -> usize {
  // A thread that finds every slot taken does not look again, and an allocation made while the
  // releaser is registered counts in the pool.
  THREAD_SLOT.with(|slot| slot.set(NO_SLOT));
  for (index, in_use) in SLOT_IN_USE.iter().enumerate() {
    if !in_use.load(Ordering::Relaxed)
      && in_use
        .compare_exchange(false, true, Ordering::Acquire, Ordering::Relaxed)
        .is_ok()
    {
      // Registers the releaser, which hands the slot over when the thread exits.
      let _ = THREAD_SLOT_RELEASER.try_with(|_| ());
      THREAD_SLOT.with(|slot| slot.set(index));
      return index;
    }
  }
  NO_SLOT
}

/// Hands the slot of an exiting thread over to the next thread. The slot keeps its credit, which
/// the next owner keeps using, so the total stays exact.
struct ThreadSlotReleaser;

impl Drop for ThreadSlotReleaser {
  fn drop(&mut self) {
    let slot = THREAD_SLOT.with(|slot| slot.replace(NO_SLOT));
    if slot < THREAD_SLOTS {
      SLOT_IN_USE[slot].store(false, Ordering::Release);
    }
  }
}

/// A global allocator that counts the bytes allocated through it and not yet freed.
///
/// Every thread reserves credit for its allocations from a shared pool in batches and spends it in
/// a slot that only it writes, with a plain load and store, so counting costs no atomic
/// read-modify-write in the common case. A slot holds at most two batches of credit, which keeps
/// the count accurate when memory is freed by another thread than the one that allocated it. A
/// thread that exits hands its slot over to the next thread, and a thread that finds every slot
/// taken counts every allocation in the pool.
///
/// Only allocations made through this allocator are counted. Memory that the module allocates
/// directly from the C allocator, for example in a library written in C, is not counted.
pub struct TrackingAllocator<A: GlobalAlloc = System> {
  inner: A,
  /// The bytes allocated plus the credit held by the slots.
  reserved: Counter,
  /// The credit of each slot, reserved but not yet allocated.
  credits: [Counter; THREAD_SLOTS],
}

impl<A: GlobalAlloc> TrackingAllocator<A> {
  /// Creates an allocator that allocates from `inner`.
  pub const fn new(inner: A) -> Self {
    Self {
      inner,
      reserved: Counter::new(),
      credits: [const { Counter::new() }; THREAD_SLOTS],
    }
  }

  /// Returns the number of bytes allocated through this allocator and not yet freed.
  ///
  /// The slots are read one after another while other threads keep allocating, so the value can be
  /// off by the credit that the slots move meanwhile, which is at most two batches per slot.
  pub fn allocated_bytes(&self) -> u64 {
    let credits = self
      .credits
      .iter()
      .map(|credit| credit.0.load(Ordering::Relaxed))
      .sum::<i64>();
    (self.reserved.0.load(Ordering::Relaxed) - credits).max(0) as u64
  }

  #[inline(always)]
  fn charge(&self, bytes: i64) {
    let slot = thread_slot();
    if slot >= THREAD_SLOTS {
      self.reserved.0.fetch_add(bytes, Ordering::Relaxed);
      return;
    }
    // Only the owning thread writes its credit, so the load and the store cannot lose an update.
    let credit = &self.credits[slot].0;
    let available = credit.load(Ordering::Relaxed);
    if available >= bytes {
      credit.store(available - bytes, Ordering::Relaxed);
      return;
    }
    self
      .reserved
      .0
      .fetch_add(bytes - available + CREDIT_BATCH, Ordering::Relaxed);
    credit.store(CREDIT_BATCH, Ordering::Relaxed);
  }

  #[inline(always)]
  fn uncharge(&self, bytes: i64) {
    let slot = thread_slot();
    if slot >= THREAD_SLOTS {
      self.reserved.0.fetch_sub(bytes, Ordering::Relaxed);
      return;
    }
    let credit = &self.credits[slot].0;
    let available = credit.load(Ordering::Relaxed) + bytes;
    if available > 2 * CREDIT_BATCH {
      // Return the credit beyond one batch to the pool, so that idle slots hold little of it.
      credit.store(CREDIT_BATCH, Ordering::Relaxed);
      self
        .reserved
        .0
        .fetch_sub(available - CREDIT_BATCH, Ordering::Relaxed);
    } else {
      credit.store(available, Ordering::Relaxed);
    }
  }
}

// SAFETY: Every method forwards to the inner allocator with the same arguments and only adds
// bookkeeping, so the inner allocator upholds the contract of `GlobalAlloc`.
unsafe impl<A: GlobalAlloc> GlobalAlloc for TrackingAllocator<A> {
  #[inline]
  unsafe fn alloc(&self, layout: Layout) -> *mut u8 {
    let memory = unsafe { self.inner.alloc(layout) };
    if !memory.is_null() {
      self.charge(layout.size() as i64);
    }
    memory
  }

  #[inline]
  unsafe fn alloc_zeroed(&self, layout: Layout) -> *mut u8 {
    let memory = unsafe { self.inner.alloc_zeroed(layout) };
    if !memory.is_null() {
      self.charge(layout.size() as i64);
    }
    memory
  }

  #[inline]
  unsafe fn dealloc(&self, memory: *mut u8, layout: Layout) {
    unsafe { self.inner.dealloc(memory, layout) };
    self.uncharge(layout.size() as i64);
  }

  #[inline]
  unsafe fn realloc(&self, memory: *mut u8, layout: Layout, new_size: usize) -> *mut u8 {
    let new_memory = unsafe { self.inner.realloc(memory, layout, new_size) };
    if !new_memory.is_null() {
      let (old_size, new_size) = (layout.size() as i64, new_size as i64);
      if new_size > old_size {
        self.charge(new_size - old_size);
      } else {
        self.uncharge(old_size - new_size);
      }
    }
    new_memory
  }
}

/// Installs a [`TrackingAllocator`] that allocates from the system allocator as the global
/// allocator of the module, and reports the bytes it counts to Envoy as the heap memory of the
/// module.
///
/// The allocator registers a thread-local destructor on every thread that allocates through it,
/// which keeps the module loaded until those threads exit.
///
/// # Example
///
/// ```
/// envoy_proxy_dynamic_modules_rust_sdk::declare_tracking_allocator!();
/// ```
#[macro_export]
macro_rules! declare_tracking_allocator {
  () => {
    #[global_allocator]
    static ENVOY_DYNAMIC_MODULE_TRACKING_ALLOCATOR: $crate::memory::TrackingAllocator =
      $crate::memory::TrackingAllocator::new(::std::alloc::System);

    $crate::ffi_export! {
      fn envoy_dynamic_module_on_program_get_memory_usage() -> u64 {
        ENVOY_DYNAMIC_MODULE_TRACKING_ALLOCATOR.allocated_bytes()
      }
      on_panic = 0
    }
  };
}

#[cfg(test)]
mod tests {
  use super::*;
  use std::sync::{Arc, Barrier, Mutex, MutexGuard, PoisonError};

  // The slots are owned process wide, so the tests that hold or churn them must not overlap.
  static SLOT_TEST_LOCK: Mutex<()> = Mutex::new(());

  fn serialize_slot_tests() -> MutexGuard<'static, ()> {
    SLOT_TEST_LOCK
      .lock()
      .unwrap_or_else(PoisonError::into_inner)
  }

  fn layout(size: usize) -> Layout {
    Layout::from_size_align(size, 8).unwrap()
  }

  #[test]
  fn counts_allocated_bytes() {
    let allocator = TrackingAllocator::new(System);
    unsafe {
      let memory = allocator.alloc(layout(100));
      assert_eq!(allocator.allocated_bytes(), 100);
      let zeroed = allocator.alloc_zeroed(layout(50));
      assert_eq!(*zeroed, 0);
      assert_eq!(allocator.allocated_bytes(), 150);
      let grown = allocator.realloc(memory, layout(100), 300);
      assert_eq!(allocator.allocated_bytes(), 350);
      let shrunk = allocator.realloc(grown, layout(300), 10);
      assert_eq!(allocator.allocated_bytes(), 60);
      allocator.dealloc(shrunk, layout(10));
      allocator.dealloc(zeroed, layout(50));
    }
    assert_eq!(allocator.allocated_bytes(), 0);
  }

  /// An allocator whose allocations always fail.
  struct FailingAllocator;

  unsafe impl GlobalAlloc for FailingAllocator {
    unsafe fn alloc(&self, _layout: Layout) -> *mut u8 {
      std::ptr::null_mut()
    }

    unsafe fn dealloc(&self, _memory: *mut u8, _layout: Layout) {}

    unsafe fn realloc(&self, _memory: *mut u8, _layout: Layout, _new_size: usize) -> *mut u8 {
      std::ptr::null_mut()
    }
  }

  #[test]
  fn failed_allocations_are_not_counted() {
    let allocator = TrackingAllocator::new(FailingAllocator);
    unsafe {
      assert!(allocator.alloc(layout(100)).is_null());
      assert!(allocator.alloc_zeroed(layout(100)).is_null());
      let mut memory = 0u64;
      let memory = &mut memory as *mut u64 as *mut u8;
      assert!(allocator.realloc(memory, layout(8), 100).is_null());
    }
    assert_eq!(allocator.allocated_bytes(), 0);
  }

  #[test]
  fn counts_memory_freed_by_another_thread() {
    let allocator = Arc::new(TrackingAllocator::new(System));
    let memory = {
      let allocator = allocator.clone();
      std::thread::spawn(move || unsafe { allocator.alloc(layout(512)) as usize })
        .join()
        .unwrap()
    };
    assert_eq!(allocator.allocated_bytes(), 512);
    unsafe { allocator.dealloc(memory as *mut u8, layout(512)) };
    assert_eq!(allocator.allocated_bytes(), 0);
  }

  #[test]
  fn credit_stays_bounded_when_another_thread_frees() {
    let allocator = Arc::new(TrackingAllocator::new(System));
    let (sender, receiver) = std::sync::mpsc::sync_channel::<usize>(16);
    let producer = {
      let allocator = allocator.clone();
      std::thread::spawn(move || {
        for _ in 0..1000 {
          sender
            .send(unsafe { allocator.alloc(layout(1 << 20)) } as usize)
            .unwrap();
        }
      })
    };
    for memory in receiver {
      unsafe { allocator.dealloc(memory as *mut u8, layout(1 << 20)) };
    }
    producer.join().unwrap();
    assert!(allocator
      .credits
      .iter()
      .all(|credit| credit.0.load(Ordering::Relaxed) <= 2 * CREDIT_BATCH));
    assert_eq!(allocator.allocated_bytes(), 0);
  }

  #[test]
  fn threads_beyond_slots_count_in_the_pool() {
    let _serialized = serialize_slot_tests();
    const THREADS: usize = THREAD_SLOTS + 64;
    let allocator = Arc::new(TrackingAllocator::new(System));
    let all_alive = Arc::new(Barrier::new(THREADS));
    let without_slot = Arc::new(AtomicI64::new(0));
    let threads: Vec<_> = (0..THREADS)
      .map(|_| {
        let (allocator, all_alive, without_slot) =
          (allocator.clone(), all_alive.clone(), without_slot.clone());
        std::thread::spawn(move || {
          let memory = unsafe { allocator.alloc(layout(64)) };
          // Keeping every thread alive at once leaves at least 64 of them without a slot.
          all_alive.wait();
          if thread_slot() == NO_SLOT {
            without_slot.fetch_add(1, Ordering::Relaxed);
          }
          unsafe { allocator.dealloc(memory, layout(64)) };
        })
      })
      .collect();
    for thread in threads {
      thread.join().unwrap();
    }
    assert!(without_slot.load(Ordering::Relaxed) >= 64);
    assert_eq!(allocator.allocated_bytes(), 0);
  }

  #[test]
  fn exiting_threads_hand_their_slots_over() {
    let _serialized = serialize_slot_tests();
    let allocator = Arc::new(TrackingAllocator::new(System));
    let mut blocks = Vec::with_capacity(4 * THREAD_SLOTS);
    for _ in 0..(4 * THREAD_SLOTS) {
      let allocator = allocator.clone();
      let (memory, slot) = std::thread::spawn(move || {
        // Each thread leaves memory allocated, so the count is only right if the slots of exited
        // threads keep their credit.
        (
          unsafe { allocator.alloc(layout(16)) } as usize,
          thread_slot(),
        )
      })
      .join()
      .unwrap();
      assert!(slot < THREAD_SLOTS);
      blocks.push(memory);
    }
    assert_eq!(allocator.allocated_bytes(), (16 * 4 * THREAD_SLOTS) as u64);
    for memory in blocks {
      unsafe { allocator.dealloc(memory as *mut u8, layout(16)) };
    }
    assert_eq!(allocator.allocated_bytes(), 0);
  }
}
