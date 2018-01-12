#pragma once

#include <pthread.h>

#include <algorithm>
#include <string>

#include "common/common/assert.h"
#include "common/common/hash.h"
#include "common/common/logger.h"

#include "absl/base/thread_annotations.h"
#include "absl/strings/string_view.h"
#include "fmt/format.h"

namespace Envoy {

/**
 * Initialization parameters for SharedHashMap. The options are duplicated
 * to the control-block after init, to aid with sanity checking when attaching
 * an existing memory segment.
 */
struct SharedHashMapOptions {
  std::string toString() const {
    return fmt::format("capacity={}, max_key_size={}, num_slots={}", capacity, max_key_size,
                       num_slots);
  }
  bool operator==(const SharedHashMapOptions& that) const {
    return capacity == that.capacity && max_key_size == that.max_key_size &&
           num_slots == that.num_slots;
  }
  bool operator!=(const SharedHashMapOptions& that) const { return !(*this == that); }

  uint32_t capacity;     // how many values can be stored.
  uint32_t max_key_size; // how many bytes of string can be stored.
  uint32_t num_slots;    // determines speed of hash vs size efficiency.
};

/**
 * Implements hash_map<string, Value> without using pointers, suitable
 * for use in shared memory. Users must commit to capacity, max_key_size,
 * and num_slots at construction time, and to the payload value at compile-time.
 *
 * This map may also be suitable for persisting a hash-table to long term storage,
 * but not across machine architectures, as it doesn't use network byte order for
 * storing ints.
 */
template <class Value> class SharedHashMap : public Logger::Loggable<Logger::Id::config> {
public:
  /**
   * Sentinal used for next_cell links to indicate end-of-list.
   */
  static const uint32_t Sentinal = 0xffffffff;

  /**
   * Constructs a map control structure given a set of options, which cannot be changed.
   * Note that the map dtaa itself is not constructed when the object is constructed.
   * After the control-structure is constructed, the number of bytes can be computed so
   * that a shared-memory segment can be allocated and passed to init() or attach().
   */
  SharedHashMap(const SharedHashMapOptions& options)
      : options_(options), cells_(nullptr), control_(nullptr), slots_(nullptr) {}

  /**
   * Returns the numbers of byte required for the hash-table, based on
   * the control structure. This must be used to allocate the
   * backing-store (eg) in shared memory, which we do after
   * constructing the object with the desired sizing.
   */
  size_t numBytes() const {
    size_t size =
        cellOffset(options_.capacity) + sizeof(Control) + options_.num_slots * sizeof(uint32_t);
    return align(size);
  }

  /**
   * Attempts to attach to an existing shared memory segment. Does a (relatively) quick
   * sanity check to make sure the options copied to the provided memory match, and also
   * that the slot, cell, and key-string structures look sane.
   *
   * Note that if mutex is in a locked state at the time of attachment, this function
   * can hang.
   */
  bool attach(uint8_t* memory) {
    mapMemorySegments(memory);
    return sanityCheck();
  }

  /** Locks the map and runs sanity checks */
  bool sanityCheck() LOCKS_EXCLUDED(control_->mutex) {
    lock(); // might hang if program previously crashed.
    bool ret = sanityCheckLockHeld();
    unlock();
    return ret;
  }

  /**
   * Returns a string describing the contents of the map, including the control
   * bits and the keys in each slot.
   */
  std::string toString() {
    std::string ret;
    lock();
    ret = fmt::format("options={}\ncontrol={}\n", options_.toString(), control_->toString());
    for (uint32_t i = 0; i < options_.num_slots; ++i) {
      ret += fmt::format("slot {}:", i);
      for (uint32_t j = slots_[i]; j != Sentinal; j = getCell(j).next_cell) {
        ret += " " + std::string(getCell(j).getKey());
      }
      ret += "\n";
    }
    unlock();
    return ret;
  }

  /**
   * Initializes a hash-map on raw memory. No expectations are made about the state of the memory
   * coming in.
   * @param memory
   */
  void init(uint8_t* memory) {
    mapMemorySegments(memory);
    lock();

    control_->options = options_;
    control_->size = 0;
    control_->free_cell_index = 0;

    // Initialize all the slots;
    for (uint32_t slot = 0; slot < options_.num_slots; ++slot) {
      slots_[slot] = Sentinal;
    }

    // Initialize all the key-char offsets.
    for (uint32_t cell_index = 0; cell_index < options_.capacity; ++cell_index) {
      Cell& cell = getCell(cell_index);
      cell.key_size = 0;
      cell.next_cell = cell_index + 1;
    }

    unlock();
  }

  /**
   * Puts a new key into the map. If successful (e.g. map has capacity)
   * then put returns a pointer to the value object, which the caller
   * can then write. Returns nullptr if the key was too large, or the
   * capacity of the map has been exceeded.
   *
   * @param key THe key must be 255 bytes or smaller.
   */
  Value* put(absl::string_view key) {
    if (key.size() > options_.max_key_size) {
      return nullptr;
    }

    lock();
    Value* value = getLockHeld(key);
    if ((value == nullptr) && (control_->size < options_.capacity)) {
      uint32_t slot = HashUtil::xxHash64(key) % options_.num_slots;
      uint32_t cell_index = control_->free_cell_index;
      Cell& cell = getCell(cell_index);
      control_->free_cell_index = cell.next_cell;
      cell.next_cell = slots_[slot];
      slots_[slot] = cell_index;
      cell.key_size = key.size();
      memcpy(cell.key, key.data(), key.size());
      value = &cell.value;
      ++control_->size;
    }
    unlock();
    return value;
  }

  /**
   * Removes the specified key from the map, returning bool if the key
   * was found.
   * @param key the key to remove
   */
  bool remove(absl::string_view key) {
    if (key.size() > options_.max_key_size) {
      return false;
    }

    bool found = false;
    lock();
    uint32_t slot = HashUtil::xxHash64(key) % options_.num_slots;
    uint32_t* next = nullptr;
    for (uint32_t* cptr = &slots_[slot]; *cptr != Sentinal; cptr = next) {
      Cell& cell = getCell(*cptr);
      next = &cell.next_cell;
      if (cell.getKey() == key) {
        control_->free_cell_index = *cptr;
        --control_->size;
        *cptr = *next; // Splices current cell out of slot-chain.
        *next = control_->free_cell_index;
        found = true;
        break;
      }
    }
    unlock();
    return found;
  }

  /** Returns the number of key/values stored in the map. */
  size_t size() const { return control_->size; }

  /**
   * Gets the value associated with a key, returning null if the value was not found.
   * @param key
   */
  Value* get(absl::string_view key) {
    if (key.size() > options_.max_key_size) {
      return nullptr;
    }

    lock();
    Value* value = getLockHeld(key);
    unlock();
    return value;
  }

private:
  /**
   * Represents control-values for the hash-table, including a mutex, which
   * must gate all access to the internals.
   */
  struct Control {
    std::string toString() const {
      return fmt::format("{} size={} free_cell_index={}", options.toString(), size,
                         free_cell_index);
    }

    pthread_mutex_t mutex;
    SharedHashMapOptions options; // Options established at map construction time.
    uint32_t size;                // Number of values currently stored.
    uint32_t free_cell_index;     // Offset of first free cell.
  };

  /**
   * Represents a value-cell, which is stored in a linked-list from each slot.
   */
  struct Cell {
    /** Returns the key as a string_view. */
    absl::string_view getKey() const { return absl::string_view(key, key_size); }

    Value value;        // Templated value field.
    uint32_t next_cell; // OFfset of next cell in map->cells_, terminated with Sentinal.
    uint8_t key_size;   // size of key in bytes, or 0 if unused.
    char key[];
  };

  // It seems like this is an obvious constexpr, but it won't compile as one.
  static size_t alignment() {
    return std::max(alignof(Cell), std::max(alignof(uint32_t), alignof(Control)));
  }

  static uint32_t align(uint32_t size) { return (size + alignment() - 1) & ~(alignment() - 1); }

  /**
   * Computes the byte offset of a cell into cells_. This is not
   * simply an array index because we don't know the size of a key at
   * compile-time.
   */
  uint32_t cellOffset(uint32_t cell_index) const {
    uint32_t cell_size = align(options_.max_key_size + sizeof(Cell));
    return cell_index * cell_size;
  }

  /**
   * Returns a reference to a Cell at the specified index.
   */
  Cell& getCell(uint32_t cell_index) {
    // Because the key-size is parameteriziable, an array-lookup on sizeof(Cell) does not work.
    char* ptr = reinterpret_cast<char*>(cells_) + cellOffset(cell_index);
    RELEASE_ASSERT((reinterpret_cast<uint64_t>(ptr) & (alignment() - 1)) == 0);
    return *reinterpret_cast<Cell*>(ptr);
  }

  /** Maps out the segments of shared memory for us to work with. */
  void mapMemorySegments(uint8_t* memory) {
    // Note that we are not examining or mutating memory here, just looking at the pointer,
    // so we don't need to hold any locks.
    cells_ = reinterpret_cast<Cell*>(memory); // First because Value may need to be aligned.
    memory += cellOffset(options_.capacity);
    control_ = reinterpret_cast<Control*>(memory);
    memory += sizeof(Control);
    slots_ = reinterpret_cast<uint32_t*>(memory);

    // TODO(jmarantz): share this code with SharedMemory::initializeMutex in hot_restart_impl.cc
    pthread_mutexattr_t attribute;
    pthread_mutexattr_init(&attribute);
    pthread_mutexattr_setpshared(&attribute, PTHREAD_PROCESS_SHARED);
    pthread_mutexattr_setrobust(&attribute, PTHREAD_MUTEX_ROBUST);
    pthread_mutex_init(&control_->mutex, &attribute);
  }

  Value* getLockHeld(absl::string_view key) EXCLUSIVE_LOCKS_REQUIRED(control_->mutex) {
    uint32_t slot = HashUtil::xxHash64(key) % options_.num_slots;
    for (uint32_t c = slots_[slot]; c != Sentinal; c = getCell(c).next_cell) {
      Cell& cell = getCell(c);
      if (cell.getKey() == key) {
        return &cell.value;
      }
    }
    return nullptr;
  }

  /** Examines the data structures to see if they are sane. Tries hard not to crash or hang. */
  bool sanityCheckLockHeld() EXCLUSIVE_LOCKS_REQUIRED(control_->mutex) {
    bool ret = true;
    if (options_ != control_->options) {
      // options doesn't match.
      ENVOY_LOG(error, "SharedHashMap options don't match");
      return false;
    }

    if (control_->size > options_.capacity) {
      ENVOY_LOG(error, "SharedHashMap size={} > capacity={}", control_->size, options_.capacity);
      return false;
    }

    // As a sanity check, makee sure there are control_->size values
    // reachable from the slots, each of which has a valid char_offset
    uint32_t num_values = 0;
    for (uint32_t slot = 0; slot < options_.num_slots; ++slot) {
      uint32_t next = 0;  // initialized to silence compilers.
      for (uint32_t cell_index = slots_[slot]; cell_index != Sentinal; cell_index = next) {
        if (cell_index >= options_.capacity) {
          ENVOY_LOG(error, "SharedHashMap cell index too high={}, capacity={}", cell_index,
                    options_.capacity);
          break;
        } else {
          Cell& cell = getCell(cell_index);
          next = cell.next_cell;
          if (cell.key_size > options_.max_key_size) {
            ENVOY_LOG(error, "SharedHashMap live cell has key_size=={}", cell.key_size);
            ret = false;
          }
          ++num_values;
          // Avoid infinite loops if there is a next_cell cycle within
          // a slot. Note that the num_values message will be emitted
          // outside the loop.
          if (num_values > control_->size) {
            break;
          }
        }
      }
    }
    if (num_values != control_->size) {
      ENVOY_LOG(error, "SharedHashMap has wrong number of live cells: {}, expected {}", num_values,
                control_->size);
      ret = false;
    }
    return ret;
  }

  /** Locks the mutex. */
  void lock() { pthread_mutex_lock(&control_->mutex); }

  /** Unlocks the mutex. */
  void unlock() { pthread_mutex_unlock(&control_->mutex); }

  // Copy of the options in process-local memory; which is used to help compute
  // the required size in bytes after construction and before init/attach.
  const SharedHashMapOptions options_;

  // Pointers into shared memory. Cells go first, because Value may need a more aggressive
  // aligmnment.
  Cell* cells_ PT_GUARDED_BY(control_->mutex);
  Control* control_;
  uint32_t* slots_ PT_GUARDED_BY(control_->mutex);
};

} // namespace Envoy
