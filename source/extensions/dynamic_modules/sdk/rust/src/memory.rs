//! Tracking of the heap memory allocated by a dynamic module.
//!
//! A module that declares a [`TrackingAllocator`] as its global allocator counts the bytes that it
//! allocates through it, and can refuse the allocations beyond a memory limit. A
//! [`MemoryResourceMonitor`] reports these bytes to the overload manager through the dynamic module
//! resource monitor, so that overload actions and load shed points can act when the module exceeds
//! its budget.
//!
//! Only a module built as a shared object of its own can use the allocator, since all the Rust
//! code linked into Envoy has a single global allocator.
//!
//! # Example
//!
//! The following module reports its heap memory against the budget in bytes that the
//! configuration of its resource monitor holds.
//!
//! ```
//! use envoy_proxy_dynamic_modules_rust_sdk::memory::{MemoryResourceMonitor, TrackingAllocator};
//! use envoy_proxy_dynamic_modules_rust_sdk::resource_monitor::{
//!   EnvoyResourceMonitorConfig, ResourceMonitor,
//! };
//! use envoy_proxy_dynamic_modules_rust_sdk::*;
//!
//! #[global_allocator]
//! static ALLOCATOR: TrackingAllocator = TrackingAllocator::new(std::alloc::System);
//!
//! declare_all_init_functions!(my_program_init, resource_monitor: my_new_resource_monitor_fn);
//!
//! fn my_program_init() -> bool {
//!   true
//! }
//!
//! fn my_new_resource_monitor_fn(
//!   _name: &str,
//!   config: &[u8],
//!   _envoy_config: &mut EnvoyResourceMonitorConfig,
//! ) -> Option<Box<dyn ResourceMonitor>> {
//!   let max_heap_size_bytes = std::str::from_utf8(config).ok()?.parse().ok()?;
//!   Some(Box::new(MemoryResourceMonitor::new(
//!     &ALLOCATOR,
//!     max_heap_size_bytes,
//!   )))
//! }
//! ```

use crate::resource_monitor::{ResourceMonitor, ResourceMonitorUpdate};
use std::alloc::{GlobalAlloc, Layout, System};
use std::cell::Cell;
use std::fmt::Write;
use std::num::NonZeroU64;
use std::sync::atomic::{AtomicBool, AtomicI64, Ordering};
use std::sync::{Mutex, MutexGuard, PoisonError};

/// The largest allocation that the allocator forwards, which keeps the arithmetic of the counters
/// within `i64`. No allocator can provide this much memory.
const MAX_ALLOCATION_BYTES: usize = 1 << 62;

/// The largest memory limit, which keeps the arithmetic of the counters within `i64`.
const MAX_LIMIT_BYTES: u64 = 1 << 62;

/// The number of threads that count their allocations in a slot of their own. A thread that finds
/// every slot taken counts in the pool instead.
const THREAD_SLOTS: usize = 256;
const UNASSIGNED_SLOT: usize = usize::MAX;
const NO_SLOT: usize = usize::MAX - 1;

/// The largest credit that a slot reserves from the pool at once.
const CREDIT_BATCH: i64 = 64 << 10;

/// A counter on a cache line of its own, so that threads counting in different slots never contend.
#[repr(align(128))]
struct Counter(AtomicI64);

impl Counter {
  const fn new() -> Self {
    Self(AtomicI64::new(0))
  }
}

/// A lock on a cache line of its own, so that the refusals that take it do not slow down counting.
#[repr(align(128))]
struct LimitUpdateLock(Mutex<()>);

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
/// the next owner keeps using, so the total stays exact. A thread whose first allocation happens
/// after its thread-local destructors ran keeps its slot.
struct ThreadSlotReleaser;

impl Drop for ThreadSlotReleaser {
  fn drop(&mut self) {
    let slot = THREAD_SLOT.with(|slot| slot.replace(NO_SLOT));
    if slot < THREAD_SLOTS {
      SLOT_IN_USE[slot].store(false, Ordering::Release);
    }
  }
}

/// The credit that a slot reserves at once under the given limit, a small fraction of the limit so
/// that the credit idle in the slots matters little, between 1 KiB and [`CREDIT_BATCH`].
fn credit_batch(limit_bytes: i64) -> i64 {
  (limit_bytes / 8192).clamp(1 << 10, CREDIT_BATCH)
}

/// A global allocator that counts the bytes allocated through it and not yet freed, and refuses an
/// allocation that would exceed its memory limit.
///
/// Every thread reserves credit for its allocations from a shared pool in batches of up to 64 KiB
/// and spends it in a slot that only it writes, with a plain load and store, so counting costs no
/// atomic read-modify-write in the common case. A slot holds at most two batches of credit, which
/// keeps the count accurate when memory is freed by a thread other than the one that allocated it.
/// Up to 256 threads hold a slot at a time. A thread that exits hands its slot over to the next
/// thread, and a thread that finds every slot taken counts every allocation in the pool.
///
/// Credit is only reserved within the limit, so the bytes allocated never exceed it, but the credit
/// that other threads hold can make the allocator refuse an allocation below the limit, by up to
/// two batches per slot. Under a limit, a batch is 1/8192 of the limit, between 1 KiB and 64 KiB.
/// Threads other than the one that sets a limit keep the credit that they hold until they next free
/// memory. A refused allocation behaves like running out of memory, which aborts the process unless
/// the module allocates with a fallible API such as `Vec::try_reserve`. On Unix, the first
/// allocation that a limit refuses is reported on the standard error, once in the life of the
/// allocator.
///
/// Only the allocations made through this allocator are counted, by the size that they request.
/// Memory that the module allocates directly from the C allocator, for example in a library written
/// in C, is not counted. A module that needs another global allocator wraps it, since a module has
/// a single global allocator.
///
/// The allocator registers a thread-local destructor on every thread that gets one of its slots,
/// which on Linux keeps the module loaded until those threads exit. A later load from the same path
/// then reuses the loaded module, even if the file changed.
pub struct TrackingAllocator<A: GlobalAlloc = System> {
  inner: A,
  /// The bytes allocated plus the credit held by the slots, which reservations keep within the
  /// limit.
  reserved: Counter,
  /// The credit of each slot, reserved but not yet allocated.
  credits: [Counter; THREAD_SLOTS],
  /// Whether the allocator has counted an allocation, which a global allocator does right away.
  counting: AtomicBool,
  limit: AtomicI64,
  batch: AtomicI64,
  limit_reached: AtomicBool,
  /// Serializes the changes of the limit, which a refusal waits for.
  limit_update_lock: LimitUpdateLock,
}

impl<A: GlobalAlloc> TrackingAllocator<A> {
  /// Creates an allocator that allocates from `inner`, without a memory limit.
  pub const fn new(inner: A) -> Self {
    Self {
      inner,
      reserved: Counter::new(),
      credits: [const { Counter::new() }; THREAD_SLOTS],
      counting: AtomicBool::new(false),
      limit: AtomicI64::new(MAX_LIMIT_BYTES as i64),
      batch: AtomicI64::new(CREDIT_BATCH),
      limit_reached: AtomicBool::new(false),
      limit_update_lock: LimitUpdateLock(Mutex::new(())),
    }
  }

  /// Returns the number of bytes that the allocations made through this allocator requested and
  /// that are not freed yet.
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

  /// Sets the memory limit, which the bytes allocated through this allocator never exceed, until
  /// another limit is set or [`TrackingAllocator::remove_limit`] removes it.
  ///
  /// Returns false and keeps the current limit if the limit is larger than 2^62 bytes, or lower
  /// than the bytes already reserved, which include the credit of the other threads.
  #[must_use]
  pub fn set_limit(&self, limit_bytes: NonZeroU64) -> bool {
    if limit_bytes.get() > MAX_LIMIT_BYTES {
      return false;
    }
    let limit_bytes = limit_bytes.get() as i64;
    let _limit_update = self.lock_limit_updates();
    self.reclaim_idle_credit();
    // The limit is stored before the reserved bytes are checked, so that a reservation made
    // meanwhile either sees the new limit or is seen by the check.
    let previous_limit = self.limit.swap(limit_bytes, Ordering::SeqCst);
    if self.reserved.0.load(Ordering::SeqCst) > limit_bytes {
      self.limit.store(previous_limit, Ordering::SeqCst);
      return false;
    }
    self
      .batch
      .store(credit_batch(limit_bytes), Ordering::Relaxed);
    true
  }

  /// Removes the memory limit.
  pub fn remove_limit(&self) {
    let _limit_update = self.lock_limit_updates();
    self.limit.store(MAX_LIMIT_BYTES as i64, Ordering::SeqCst);
    self.batch.store(CREDIT_BATCH, Ordering::Relaxed);
  }

  fn lock_limit_updates(&self) -> MutexGuard<'_, ()> {
    self
      .limit_update_lock
      .0
      .lock()
      .unwrap_or_else(PoisonError::into_inner)
  }

  /// Returns the credit of the calling thread and of the free slots to the pool, so that it does
  /// not count against a new limit.
  fn reclaim_idle_credit(&self) {
    let own_slot = THREAD_SLOT.with(Cell::get);
    for (slot, in_use) in SLOT_IN_USE.iter().enumerate() {
      let owned = slot == own_slot;
      // A free slot is held while its credit is taken, so that no thread acquires it meanwhile.
      if owned
        || (!in_use.load(Ordering::Relaxed)
          && in_use
            .compare_exchange(false, true, Ordering::Acquire, Ordering::Relaxed)
            .is_ok())
      {
        let credit = self.credits[slot].0.swap(0, Ordering::Relaxed);
        self.reserved.0.fetch_sub(credit, Ordering::Relaxed);
        if !owned {
          in_use.store(false, Ordering::Release);
        }
      }
    }
  }

  /// Reserves the needed bytes and up to `extra` more within the limit, and returns the bytes
  /// reserved, or the limit that refused them.
  #[cold]
  #[inline(never)]
  fn reserve(&self, needed: i64, extra: i64) -> Result<i64, i64> {
    if !self.counting.load(Ordering::Relaxed) {
      self.counting.store(true, Ordering::Relaxed);
    }
    self.try_reserve(needed, extra).or_else(|_| {
      // A limit being set is stored before it is checked and can still be rejected, so a refusal
      // is only final once no limit is being set.
      let _limit_update = self.lock_limit_updates();
      self.try_reserve(needed, extra)
    })
  }

  fn try_reserve(&self, needed: i64, extra: i64) -> Result<i64, i64> {
    let mut reserved = self.reserved.0.load(Ordering::Relaxed);
    loop {
      let limit = self.limit.load(Ordering::Relaxed);
      let room = limit - reserved;
      if room < needed {
        return Err(limit);
      }
      let take = (needed + extra).min(room);
      match self.claim(reserved, take) {
        Ok(()) => return Ok(take),
        Err(current) => reserved = current,
      }
    }
  }

  /// Adds `take` to the reserved bytes if they are still `reserved`, and keeps it only within the
  /// limit. Returns the reserved bytes to retry with otherwise.
  fn claim(&self, reserved: i64, take: i64) -> Result<(), i64> {
    self.reserved.0.compare_exchange_weak(
      reserved,
      reserved + take,
      Ordering::SeqCst,
      Ordering::Relaxed,
    )?;
    // A limit lowered while the reservation was made applies to it as well.
    if reserved + take > self.limit.load(Ordering::SeqCst) {
      return Err(self.reserved.0.fetch_sub(take, Ordering::Relaxed) - take);
    }
    Ok(())
  }

  /// Writes the first allocation refused under a limit to the standard error, without allocating,
  /// and returns whether it reported this one.
  #[cold]
  #[inline(never)]
  fn report_limit_reached(&self, bytes: i64, limit: i64) -> bool {
    // Without a limit, only an allocation larger than any limit is refused.
    if self.limit_reached.load(Ordering::Relaxed)
      || limit == MAX_LIMIT_BYTES as i64
      || self.limit_reached.swap(true, Ordering::Relaxed)
    {
      return false;
    }
    let mut message = StackBuffer::new();
    let _ = writeln!(
      message,
      "dynamic module reached its memory limit of {limit} bytes and refused {bytes} more bytes"
    );
    write_to_stderr(message.as_bytes());
    true
  }

  #[inline(always)]
  fn charge(&self, bytes: i64) -> bool {
    let slot = thread_slot();
    if slot >= THREAD_SLOTS {
      if let Err(limit) = self.reserve(bytes, 0) {
        self.report_limit_reached(bytes, limit);
        return false;
      }
      return true;
    }
    // Only the owning thread writes its credit, so the load and the store cannot lose an update.
    let credit = &self.credits[slot].0;
    let available = credit.load(Ordering::Relaxed);
    if available >= bytes {
      credit.store(available - bytes, Ordering::Relaxed);
      return true;
    }
    match self.reserve(bytes - available, self.batch.load(Ordering::Relaxed)) {
      Ok(reserved) => {
        credit.store(available + reserved - bytes, Ordering::Relaxed);
        true
      },
      Err(limit) => {
        self.report_limit_reached(bytes, limit);
        false
      },
    }
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
    let batch = self.batch.load(Ordering::Relaxed);
    if available > 2 * batch {
      // Return the credit beyond one batch to the pool, so that idle slots hold little of it.
      credit.store(batch, Ordering::Relaxed);
      self
        .reserved
        .0
        .fetch_sub(available - batch, Ordering::Relaxed);
    } else {
      credit.store(available, Ordering::Relaxed);
    }
  }
}

// SAFETY: Every method forwards to the inner allocator with the same arguments, or returns null
// without allocating, and only adds bookkeeping, so the inner allocator upholds the contract of
// `GlobalAlloc`.
unsafe impl<A: GlobalAlloc> GlobalAlloc for TrackingAllocator<A> {
  #[inline]
  unsafe fn alloc(&self, layout: Layout) -> *mut u8 {
    if layout.size() > MAX_ALLOCATION_BYTES || !self.charge(layout.size() as i64) {
      return std::ptr::null_mut();
    }
    let memory = unsafe { self.inner.alloc(layout) };
    if memory.is_null() {
      self.uncharge(layout.size() as i64);
    }
    memory
  }

  #[inline]
  unsafe fn alloc_zeroed(&self, layout: Layout) -> *mut u8 {
    if layout.size() > MAX_ALLOCATION_BYTES || !self.charge(layout.size() as i64) {
      return std::ptr::null_mut();
    }
    let memory = unsafe { self.inner.alloc_zeroed(layout) };
    if memory.is_null() {
      self.uncharge(layout.size() as i64);
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
    if new_size > MAX_ALLOCATION_BYTES {
      return std::ptr::null_mut();
    }
    let (old_size, new_size_bytes) = (layout.size() as i64, new_size as i64);
    if new_size_bytes > old_size && !self.charge(new_size_bytes - old_size) {
      return std::ptr::null_mut();
    }
    let new_memory = unsafe { self.inner.realloc(memory, layout, new_size) };
    if new_memory.is_null() {
      if new_size_bytes > old_size {
        self.uncharge(new_size_bytes - old_size);
      }
    } else if new_size_bytes < old_size {
      self.uncharge(old_size - new_size_bytes);
    }
    new_memory
  }
}

/// A message formatted on the stack, since the allocator cannot allocate while it refuses an
/// allocation.
struct StackBuffer {
  bytes: [u8; 160],
  length: usize,
}

impl StackBuffer {
  fn new() -> Self {
    Self {
      bytes: [0; 160],
      length: 0,
    }
  }

  fn as_bytes(&self) -> &[u8] {
    &self.bytes[.. self.length]
  }
}

impl Write for StackBuffer {
  fn write_str(&mut self, text: &str) -> std::fmt::Result {
    let end = self.length + text.len();
    if end > self.bytes.len() {
      return Err(std::fmt::Error);
    }
    self.bytes[self.length .. end].copy_from_slice(text.as_bytes());
    self.length = end;
    Ok(())
  }
}

/// Writes to the standard error on Unix, without the lock of `std::io::stderr`, which a thread that
/// waits for the allocating thread can hold. Elsewhere it writes nothing.
fn write_to_stderr(bytes: &[u8]) {
  #[cfg(unix)]
  {
    use std::os::fd::FromRawFd;
    // SAFETY: The standard error stays open for the life of the process, and the file is never
    // dropped, so the descriptor is not closed.
    let mut stderr = std::mem::ManuallyDrop::new(unsafe { std::fs::File::from_raw_fd(2) });
    let _ = std::io::Write::write_all(&mut *stderr, bytes);
  }
  #[cfg(not(unix))]
  let _ = bytes;
}

/// A resource monitor that reports the bytes counted by a [`TrackingAllocator`] as a fraction of a
/// heap size, at each refresh interval of the overload manager.
///
/// The monitor reports a failed update until the allocator has counted an allocation, so that a
/// module whose global allocator is another one does not report a pressure of 0. The Envoy
/// configuration of the resource monitor must load the same shared object as the filters of the
/// module, by the same name or local file name, since a module loaded from another file has an
/// allocator of its own.
pub struct MemoryResourceMonitor<A: GlobalAlloc + Sync + 'static = System> {
  allocator: &'static TrackingAllocator<A>,
  max_heap_size_bytes: NonZeroU64,
}

impl<A: GlobalAlloc + Sync + 'static> MemoryResourceMonitor<A> {
  /// Creates a monitor that reports the bytes counted by `allocator` as a fraction of
  /// `max_heap_size_bytes`.
  pub const fn new(
    allocator: &'static TrackingAllocator<A>,
    max_heap_size_bytes: NonZeroU64,
  ) -> Self {
    Self {
      allocator,
      max_heap_size_bytes,
    }
  }
}

impl<A: GlobalAlloc + Sync + 'static> ResourceMonitor for MemoryResourceMonitor<A> {
  fn update_resource_usage(&self) -> ResourceMonitorUpdate {
    if !self.allocator.counting.load(Ordering::Relaxed) {
      return ResourceMonitorUpdate::Failure;
    }
    ResourceMonitorUpdate::Pressure(
      self.allocator.allocated_bytes() as f64 / self.max_heap_size_bytes.get() as f64,
    )
  }
}

#[cfg(test)]
mod tests {
  use super::*;
  use std::sync::{Arc, Barrier};
  use std::time::Duration;

  // The slots are owned process-wide while every test has an allocator of its own, so the tests
  // must not overlap.
  static SLOT_TEST_LOCK: Mutex<()> = Mutex::new(());

  fn serialize_slot_tests() -> MutexGuard<'static, ()> {
    SLOT_TEST_LOCK
      .lock()
      .unwrap_or_else(PoisonError::into_inner)
  }

  fn layout(size: usize) -> Layout {
    Layout::from_size_align(size, 8).unwrap()
  }

  fn limit(bytes: u64) -> NonZeroU64 {
    NonZeroU64::new(bytes).unwrap()
  }

  /// Sets the flag when dropped, so that the threads of a test stop even when the test panics.
  struct SetOnDrop<'a>(&'a AtomicBool);

  impl Drop for SetOnDrop<'_> {
    fn drop(&mut self) {
      self.0.store(true, Ordering::Relaxed);
    }
  }

  #[test]
  fn counts_allocated_bytes() {
    let _serialized = serialize_slot_tests();
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
    let _serialized = serialize_slot_tests();
    let allocator = TrackingAllocator::new(FailingAllocator);
    unsafe {
      assert!(allocator.alloc(layout(100)).is_null());
      assert!(allocator.alloc_zeroed(layout(100)).is_null());
      let mut memory = 0u64;
      let memory = &mut memory as *mut u64 as *mut u8;
      assert!(allocator.realloc(memory, layout(8), 100).is_null());
      assert!(allocator.realloc(memory, layout(8), 4).is_null());
    }
    assert_eq!(allocator.allocated_bytes(), 0);
  }

  #[test]
  fn refuses_allocations_larger_than_any_memory() {
    let _serialized = serialize_slot_tests();
    let allocator = TrackingAllocator::new(System);
    let huge = Layout::from_size_align(isize::MAX as usize - 7, 8).unwrap();
    unsafe {
      assert!(allocator.alloc(huge).is_null());
      assert!(allocator.alloc_zeroed(huge).is_null());
      let memory = allocator.alloc(layout(8));
      assert!(allocator.realloc(memory, layout(8), huge.size()).is_null());
      assert_eq!(allocator.allocated_bytes(), 8);
      allocator.dealloc(memory, layout(8));
    }
    assert_eq!(allocator.allocated_bytes(), 0);
  }

  #[test]
  fn counts_memory_freed_by_another_thread() {
    let _serialized = serialize_slot_tests();
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
    let _serialized = serialize_slot_tests();
    let allocator = Arc::new(TrackingAllocator::new(System));
    let (sender, receiver) = std::sync::mpsc::sync_channel::<usize>(16);
    let producer = {
      let allocator = allocator.clone();
      std::thread::spawn(move || {
        for _ in 0 .. 1000 {
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
  fn rejects_invalid_limits() {
    let _serialized = serialize_slot_tests();
    let allocator = TrackingAllocator::new(System);
    assert!(!allocator.set_limit(limit(MAX_LIMIT_BYTES + 1)));
    let memory = unsafe { allocator.alloc(layout(4096)) };
    assert!(!allocator.set_limit(limit(4095)));
    assert!(allocator.set_limit(limit(4096)));
    assert!(allocator.set_limit(limit(MAX_LIMIT_BYTES)));
    unsafe { allocator.dealloc(memory, layout(4096)) };
  }

  #[test]
  fn rejected_limit_keeps_the_previous_limit() {
    let _serialized = serialize_slot_tests();
    let allocator = TrackingAllocator::new(System);
    assert!(allocator.set_limit(limit(1 << 20)));
    unsafe {
      let half = allocator.alloc(layout(512 << 10));
      assert!(!allocator.set_limit(limit(256 << 10)));
      // The previous limit still applies, so 256 KiB more fits and 1 MiB more does not.
      let more = allocator.alloc(layout(256 << 10));
      assert!(!more.is_null());
      assert!(allocator.alloc(layout(1 << 20)).is_null());
      allocator.dealloc(more, layout(256 << 10));
      allocator.dealloc(half, layout(512 << 10));
    }
  }

  #[test]
  fn rejected_limit_refuses_nothing() {
    let _serialized = serialize_slot_tests();
    let allocator = TrackingAllocator::new(System);
    let held = unsafe { allocator.alloc(layout(16 << 20)) };
    let stop = AtomicBool::new(false);
    let refused = AtomicI64::new(0);
    let accepted = std::thread::scope(|scope| {
      for _ in 0 .. 4 {
        scope.spawn(|| {
          while !stop.load(Ordering::Relaxed) {
            // Larger than a batch, so that every allocation reserves from the pool.
            let memory = unsafe { allocator.alloc(layout(256 << 10)) };
            if memory.is_null() {
              refused.fetch_add(1, Ordering::Relaxed);
            } else {
              unsafe { allocator.dealloc(memory, layout(256 << 10)) };
            }
          }
        });
      }
      let _stop = SetOnDrop(&stop);
      // More memory than the limit is in use, so every limit is rejected.
      (0 .. 5000)
        .filter(|_| allocator.set_limit(limit(8 << 20)))
        .count()
    });
    assert_eq!(accepted, 0);
    assert_eq!(refused.load(Ordering::Relaxed), 0);
    unsafe { allocator.dealloc(held, layout(16 << 20)) };
  }

  #[test]
  fn refusal_waits_for_a_limit_being_set() {
    let _serialized = serialize_slot_tests();
    let allocator = TrackingAllocator::new(System);
    assert!(allocator.set_limit(limit(1 << 20)));
    std::thread::scope(|scope| {
      // Stores a lower limit under the lock, as set_limit does before it rejects the limit.
      let limit_update = allocator.lock_limit_updates();
      allocator.limit.store(4096, Ordering::SeqCst);
      let reservation = scope.spawn(|| allocator.reserve(8192, 0));
      // Gives the reservation time to be refused early if it does not wait for the lock.
      std::thread::sleep(Duration::from_millis(100));
      assert!(!reservation.is_finished());
      allocator.limit.store(1 << 20, Ordering::SeqCst);
      drop(limit_update);
      assert_eq!(reservation.join().unwrap(), Ok(8192));
    });
  }

  #[test]
  fn refusal_returns_the_accepted_limit_that_refused_it() {
    let _serialized = serialize_slot_tests();
    let allocator = TrackingAllocator::new(System);
    assert!(allocator.set_limit(limit(1 << 20)));
    std::thread::scope(|scope| {
      // Stores a lower limit under the lock, as set_limit does before it rejects the limit.
      let limit_update = allocator.lock_limit_updates();
      allocator.limit.store(4096, Ordering::SeqCst);
      let reservation = scope.spawn(|| allocator.reserve(2 << 20, 0));
      // Gives the reservation time to be refused early if it does not wait for the lock.
      std::thread::sleep(Duration::from_millis(100));
      allocator.limit.store(1 << 20, Ordering::SeqCst);
      drop(limit_update);
      assert_eq!(reservation.join().unwrap(), Err(1 << 20));
    });
    unsafe { assert!(allocator.alloc(layout(2 << 20)).is_null()) };
    assert!(allocator.limit_reached.load(Ordering::Relaxed));
  }

  #[test]
  fn lowered_limit_applies_to_a_reservation_made_meanwhile() {
    let _serialized = serialize_slot_tests();
    let allocator = TrackingAllocator::new(System);
    assert!(allocator.set_limit(limit(1 << 20)));
    // A reservation computed under the higher limit claims its bytes after a lower one is accepted.
    let reserved = allocator.reserved.0.load(Ordering::Relaxed);
    assert!(allocator.set_limit(limit(4096)));
    assert_eq!(allocator.claim(reserved, 8192), Err(reserved));
    assert_eq!(allocator.reserved.0.load(Ordering::Relaxed), reserved);
  }

  #[test]
  fn limit_changes_wait_for_each_other() {
    let _serialized = serialize_slot_tests();
    let allocator = TrackingAllocator::new(System);
    std::thread::scope(|scope| {
      let limit_update = allocator.lock_limit_updates();
      let setter = scope.spawn(|| allocator.set_limit(limit(1 << 20)));
      let remover = scope.spawn(|| allocator.remove_limit());
      // Gives the changes time to finish early if they do not wait for the lock.
      std::thread::sleep(Duration::from_millis(100));
      assert!(!setter.is_finished());
      assert!(!remover.is_finished());
      drop(limit_update);
      assert!(setter.join().unwrap());
      remover.join().unwrap();
    });
  }

  #[test]
  fn refusals_without_a_limit_are_not_reported() {
    let _serialized = serialize_slot_tests();
    let allocator = TrackingAllocator::new(System);
    unsafe {
      let memory = allocator.alloc(layout(64));
      // Without a limit, only an allocation larger than any limit is refused.
      assert!(allocator.alloc(layout(MAX_ALLOCATION_BYTES)).is_null());
      assert!(!allocator.limit_reached.load(Ordering::Relaxed));
      allocator.dealloc(memory, layout(64));
    }
    assert_eq!(allocator.allocated_bytes(), 0);
  }

  #[test]
  fn only_the_first_refusal_is_reported() {
    let _serialized = serialize_slot_tests();
    let allocator = TrackingAllocator::new(System);
    assert!(allocator.set_limit(limit(4096)));
    assert!(allocator.report_limit_reached(8192, 4096));
    assert!(!allocator.report_limit_reached(8192, 4096));
  }

  #[test]
  fn removed_limit_refuses_nothing() {
    let _serialized = serialize_slot_tests();
    let allocator = TrackingAllocator::new(System);
    assert!(allocator.set_limit(limit(4096)));
    allocator.remove_limit();
    unsafe {
      let memory = allocator.alloc(layout(1 << 20));
      assert!(!memory.is_null());
      // Without a limit, a slot again reserves a full batch beyond what it allocates.
      assert_eq!(
        allocator.reserved.0.load(Ordering::Relaxed),
        (1 << 20) + CREDIT_BATCH
      );
      allocator.dealloc(memory, layout(1 << 20));
    }
    assert_eq!(allocator.allocated_bytes(), 0);
  }

  #[test]
  fn credit_of_other_threads_counts_against_a_new_limit() {
    let _serialized = serialize_slot_tests();
    let allocator = TrackingAllocator::new(System);
    let (allocated, exit) = (Barrier::new(2), Barrier::new(2));
    let rejected_while_alive = std::thread::scope(|scope| {
      let thread = scope.spawn(|| unsafe {
        allocator.dealloc(allocator.alloc(layout(64 << 10)), layout(64 << 10));
        allocated.wait();
        exit.wait();
      });
      allocated.wait();
      let rejected = !allocator.set_limit(limit(64 << 10));
      exit.wait();
      // Joining waits for the thread to hand its slot over, which the end of the scope does not.
      thread.join().unwrap();
      rejected
    });
    // The credit of a live thread can still be spent, while the slot of an exited thread is free.
    assert!(rejected_while_alive);
    assert!(allocator.set_limit(limit(64 << 10)));
  }

  #[test]
  fn limit_sets_the_credit_batch() {
    let _serialized = serialize_slot_tests();
    for (limit_bytes, batch) in [
      (1 << 20, 1 << 10),
      (64 << 20, 8 << 10),
      (1 << 30, CREDIT_BATCH),
    ] {
      let allocator = TrackingAllocator::new(System);
      assert!(allocator.set_limit(limit(limit_bytes)));
      unsafe {
        let memory = allocator.alloc(layout(1));
        assert_eq!(
          allocator.reserved.0.load(Ordering::Relaxed),
          1 + batch,
          "limit of {limit_bytes} bytes"
        );
        // Freeing more than two batches returns the credit beyond one batch to the pool.
        let block = allocator.alloc(layout(4 * batch as usize));
        allocator.dealloc(block, layout(4 * batch as usize));
        assert_eq!(
          allocator.reserved.0.load(Ordering::Relaxed),
          1 + batch,
          "limit of {limit_bytes} bytes"
        );
        allocator.dealloc(memory, layout(1));
      }
    }
  }

  #[test]
  fn threads_without_a_slot_respect_the_limit() {
    let _serialized = serialize_slot_tests();
    let allocator = TrackingAllocator::new(System);
    assert!(allocator.set_limit(limit(1 << 20)));
    std::thread::scope(|scope| {
      scope.spawn(|| unsafe {
        THREAD_SLOT.with(|slot| slot.set(NO_SLOT));
        let memory = allocator.alloc(layout(1 << 20));
        assert!(!memory.is_null());
        assert!(allocator.alloc(layout(1)).is_null());
        allocator.dealloc(memory, layout(1 << 20));
      });
    });
    assert_eq!(allocator.allocated_bytes(), 0);
  }

  #[test]
  fn credit_held_before_a_limit_does_not_exceed_it() {
    let _serialized = serialize_slot_tests();
    let allocator = TrackingAllocator::new(System);
    unsafe {
      let memory = allocator.alloc(layout(64 << 10));
      allocator.dealloc(memory, layout(64 << 10));
      assert!(allocator.set_limit(limit(4096)));
      assert!(allocator.alloc(layout(100 << 10)).is_null());
    }
    assert!(allocator.reserved.0.load(Ordering::Relaxed) <= 4096);
  }

  #[test]
  fn refuses_allocations_beyond_limit() {
    let _serialized = serialize_slot_tests();
    let allocator = TrackingAllocator::new(System);
    assert!(allocator.set_limit(limit(1 << 20)));
    unsafe {
      let half = allocator.alloc(layout(512 << 10));
      assert!(!half.is_null());
      assert!(allocator.alloc(layout(1 << 20)).is_null());
      assert!(allocator.alloc_zeroed(layout(1 << 20)).is_null());
      // A refused reallocation leaves the memory as it was.
      assert!(allocator
        .realloc(half, layout(512 << 10), 2 << 20)
        .is_null());
      assert_eq!(allocator.allocated_bytes(), 512 << 10);
      allocator.dealloc(half, layout(512 << 10));
      let full = allocator.alloc(layout(1 << 20));
      assert!(!full.is_null());
      assert_eq!(allocator.reserved.0.load(Ordering::Relaxed), 1 << 20);
      allocator.dealloc(full, layout(1 << 20));
    }
    assert_eq!(allocator.allocated_bytes(), 0);
  }

  fn xorshift(state: &mut u64) -> u64 {
    *state ^= *state << 13;
    *state ^= *state >> 7;
    *state ^= *state << 17;
    *state
  }

  #[test]
  fn limit_is_never_exceeded() {
    const LIMIT: i64 = 4 << 20;
    let _serialized = serialize_slot_tests();
    let allocator = Arc::new(TrackingAllocator::new(System));
    assert!(allocator.set_limit(limit(LIMIT as u64)));
    let outstanding = Arc::new(AtomicI64::new(0));
    let most_outstanding = Arc::new(AtomicI64::new(0));
    let most_reserved = Arc::new(AtomicI64::new(0));
    let stop = Arc::new(AtomicBool::new(false));
    let monitor = {
      let (allocator, outstanding, most_outstanding, most_reserved, stop) = (
        allocator.clone(),
        outstanding.clone(),
        most_outstanding.clone(),
        most_reserved.clone(),
        stop.clone(),
      );
      std::thread::spawn(move || {
        while !stop.load(Ordering::Relaxed) {
          most_outstanding.fetch_max(outstanding.load(Ordering::Relaxed), Ordering::Relaxed);
          most_reserved.fetch_max(
            allocator.reserved.0.load(Ordering::Relaxed),
            Ordering::Relaxed,
          );
        }
      })
    };
    let threads: Vec<_> = (0 .. 16u64)
      .map(|seed| {
        let (allocator, outstanding) = (allocator.clone(), outstanding.clone());
        std::thread::spawn(move || {
          let mut random = seed.wrapping_mul(0x9E37_79B9_7F4A_7C15) | 1;
          let mut held = Vec::with_capacity(256);
          for _ in 0 .. 50_000 {
            if held.len() < 256 && !xorshift(&mut random).is_multiple_of(3) {
              let size = (xorshift(&mut random) % (256 << 10)) as usize + 1;
              let memory = unsafe { allocator.alloc(layout(size)) };
              if !memory.is_null() {
                // Published after the allocation succeeded, as any user of the memory would.
                outstanding.fetch_add(size as i64, Ordering::Relaxed);
                held.push((memory as usize, size));
              }
            } else if let Some((memory, size)) = held.pop() {
              outstanding.fetch_sub(size as i64, Ordering::Relaxed);
              unsafe { allocator.dealloc(memory as *mut u8, layout(size)) };
            }
          }
          for (memory, size) in held {
            outstanding.fetch_sub(size as i64, Ordering::Relaxed);
            unsafe { allocator.dealloc(memory as *mut u8, layout(size)) };
          }
        })
      })
      .collect();
    for thread in threads {
      thread.join().unwrap();
    }
    stop.store(true, Ordering::Relaxed);
    monitor.join().unwrap();
    assert!(most_outstanding.load(Ordering::Relaxed) <= LIMIT);
    assert!(most_reserved.load(Ordering::Relaxed) <= LIMIT);
    assert!(allocator.limit_reached.load(Ordering::Relaxed));
    assert_eq!(allocator.allocated_bytes(), 0);
  }

  #[test]
  fn threads_beyond_slots_count_in_the_pool() {
    let _serialized = serialize_slot_tests();
    const THREADS: usize = THREAD_SLOTS + 64;
    let allocator = Arc::new(TrackingAllocator::new(System));
    // The test thread waits with the others, so that it counts while every thread holds memory.
    let all_alive = Arc::new(Barrier::new(THREADS + 1));
    let counted = Arc::new(Barrier::new(THREADS + 1));
    let without_slot = Arc::new(AtomicI64::new(0));
    let threads: Vec<_> = (0 .. THREADS)
      .map(|_| {
        let (allocator, all_alive, counted, without_slot) = (
          allocator.clone(),
          all_alive.clone(),
          counted.clone(),
          without_slot.clone(),
        );
        std::thread::spawn(move || {
          let memory = unsafe { allocator.alloc(layout(64)) };
          // Keeping every thread alive at once leaves at least 64 of them without a slot.
          all_alive.wait();
          if thread_slot() == NO_SLOT {
            without_slot.fetch_add(1, Ordering::Relaxed);
          }
          counted.wait();
          unsafe { allocator.dealloc(memory, layout(64)) };
        })
      })
      .collect();
    all_alive.wait();
    let allocated_while_alive = allocator.allocated_bytes();
    counted.wait();
    for thread in threads {
      thread.join().unwrap();
    }
    assert!(without_slot.load(Ordering::Relaxed) >= 64);
    assert_eq!(allocated_while_alive, (64 * THREADS) as u64);
    assert_eq!(allocator.allocated_bytes(), 0);
  }

  #[test]
  fn exiting_threads_hand_their_slots_over() {
    let _serialized = serialize_slot_tests();
    let allocator = Arc::new(TrackingAllocator::new(System));
    let mut blocks = Vec::with_capacity(4 * THREAD_SLOTS);
    for _ in 0 .. (4 * THREAD_SLOTS) {
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

  #[test]
  fn monitor_reports_allocated_bytes_as_pressure() {
    static ALLOCATOR: TrackingAllocator = TrackingAllocator::new(System);
    let _serialized = serialize_slot_tests();
    let monitor = MemoryResourceMonitor::new(&ALLOCATOR, NonZeroU64::new(4096).unwrap());
    // An allocator that has counted nothing is not the global allocator of the module.
    assert_eq!(
      monitor.update_resource_usage(),
      ResourceMonitorUpdate::Failure
    );
    unsafe {
      let memory = ALLOCATOR.alloc(layout(1024));
      assert_eq!(
        monitor.update_resource_usage(),
        ResourceMonitorUpdate::Pressure(0.25)
      );
      // Envoy treats a pressure above 1 as 1, so the monitor reports it as it is.
      let more = ALLOCATOR.alloc(layout(7168));
      assert_eq!(
        monitor.update_resource_usage(),
        ResourceMonitorUpdate::Pressure(2.0)
      );
      ALLOCATOR.dealloc(more, layout(7168));
      ALLOCATOR.dealloc(memory, layout(1024));
    }
    assert_eq!(
      monitor.update_resource_usage(),
      ResourceMonitorUpdate::Pressure(0.0)
    );
  }
}
