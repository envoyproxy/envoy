#pragma once

#include <algorithm>
#include <string>
#include <utility>

#include "common/common/assert.h"
#include "common/common/hash.h"
#include "common/common/logger.h"

#include "absl/strings/string_view.h"
#include "fmt/format.h"

namespace Envoy {

/**
 * Initialization parameters for SharedMemoryHashSet. The options are duplicated
 * to the control-block after init, to aid with sanity checking when attaching
 * an existing memory segment.
 */
struct SharedMemoryHashSetOptions {
  std::string toString() const {
    return fmt::format("capacity={}, num_slots={}", capacity, num_slots);
  }
  bool operator==(const SharedMemoryHashSetOptions& that) const {
    return capacity == that.capacity && num_slots == that.num_slots;
  }
  bool operator!=(const SharedMemoryHashSetOptions& that) const { return !(*this == that); }

  uint32_t capacity;  // how many values can be stored.
  uint32_t num_slots; // determines speed of hash vs size efficiency.
};

/**
 * Implements hash_set<Value> without using pointers, suitable for use
 * in shared memory. Users must commit to capacity and num_slots at
 * construction time. Value must provide these methods:
 *    absl::string_view Value::key()
 *    void Value::initialize(absl::string_view key)
 *    static size_t Value::size()
 *
 * This set may also be suitable for persisting a hash-table to long
 * term storage, but not across machine architectures, as it doesn't
 * use network byte order for storing ints.
 */
template <class Value> class SharedMemoryHashSet : public Logger::Loggable<Logger::Id::config> {
public:
  /**
   * Sentinal used for next_cell links to indicate end-of-list.
   */
  static const uint32_t Sentinal = 0xffffffff;

  /** Type used by put() to indicate the value at a key, and whether it was created */
  typedef std::pair<Value*, bool> ValueCreatedPair;

  /**
   * Constructs a map control structure given a set of options, which cannot be changed.
   * Note that the map data itself is not constructed when the object is constructed.
   * After the control-structure is constructed, the number of bytes can be computed so
   * that a shared-memory segment can be allocated and passed to init() or attach().
   */
  SharedMemoryHashSet(const SharedMemoryHashSetOptions& options)
      : options_(options), cells_(nullptr), control_(nullptr), slots_(nullptr) {}

  /**
   * Returns the numbers of byte required for the hash-table, based on
   * the control structure. This must be used to allocate the
   * backing-store (eg) in shared memory, which we do after
   * constructing the object with the desired sizing.
   */
  static uint32_t numBytes(const SharedMemoryHashSetOptions& options) {
    uint32_t size =
        cellOffset(options.capacity) + sizeof(Control) + options.num_slots * sizeof(uint32_t);
    return align(size);
  }

  uint32_t numBytes() const { return numBytes(options_); }

  /**
   * Returns the options structure that was used to construct the set.
   */
  const SharedMemoryHashSetOptions& options() const { return options_; }

  /**
   * Attempts to attach to an existing shared memory segment. Does a (relatively) quick
   * sanity check to make sure the options copied to the provided memory match, and also
   * that the slot, cell, and key-string structures look sane.
   *
   * Note that if mutex is in a locked state at the time of attachment, this function
   * can hang.
   */
  bool attach(uint8_t* memory, uint32_t num_bytes) {
    mapMemorySegments(memory);
    if (num_bytes != control_->num_bytes) {
      ENVOY_LOG(error, "SharedMemoryHashSet unexpected memory size {} != {}",
                num_bytes, control_->num_bytes);
      return false;
    }
    return sanityCheck();
  }

  /** Examines the data structures to see if they are sane. Tries hard not to crash or hang. */
  bool sanityCheck() {
    bool ret = true;
    if (options_ != control_->options) {
      // options doesn't match.
      ENVOY_LOG(error, "SharedMemoryHashSet options don't match");
      return false;
    }

    if (control_->size > options_.capacity) {
      ENVOY_LOG(error, "SharedMemoryHashSet size={} > capacity={}", control_->size,
                options_.capacity);
      return false;
    }

    // As a sanity check, make sure there are control_->size values
    // reachable from the slots, each of which has a valid char_offset
    uint32_t num_values = 0;
    for (uint32_t slot = 0; slot < options_.num_slots; ++slot) {
      uint32_t next = 0; // initialized to silence compilers.
      for (uint32_t cell_index = slots_[slot]; cell_index != Sentinal; cell_index = next) {
        if (cell_index >= options_.capacity) {
          ENVOY_LOG(error, "SharedMemoryHashSet cell index too high={}, capacity={}", cell_index,
                    options_.capacity);
          ret = false;
          break;
        } else {
          Cell& cell = getCell(cell_index);
          absl::string_view key = cell.value.key();
          if (computeSlot(key) != slot) {
            ENVOY_LOG(error, "SharedMemoryHashSet hash mismatch for key={}", std::string(key));
            ret = false;
          }
          next = cell.next_cell;
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
      ENVOY_LOG(error, "SharedMemoryHashSet has wrong number of live cells: {}, expected {}",
                num_values, control_->size);
      ret = false;
    }
    return ret;
  }

  /**
   * Returns a string describing the contents of the map, including the control
   * bits and the keys in each slot.
   */
  std::string toString() {
    std::string ret;
    ret = fmt::format("options={}\ncontrol={}\n", options_.toString(), control_->toString());
    for (uint32_t i = 0; i < options_.num_slots; ++i) {
      ret += fmt::format("slot {}:", i);
      for (uint32_t j = slots_[i]; j != Sentinal; j = getCell(j).next_cell) {
        ret += " " + std::string(getCell(j).value.key());
      }
      ret += "\n";
    }
    return ret;
  }

  /**
   * Initializes a hash-map on raw memory. No expectations are made about the state of the memory
   * coming in.
   * @param memory
   */
  void init(uint8_t* memory, uint32_t num_bytes) {
    mapMemorySegments(memory);
    RELEASE_ASSERT(num_bytes == numBytes(options_));
    control_->num_bytes = num_bytes;
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
      cell.next_cell = cell_index + 1;
    }
  }

  /**
   * Inserts a value into the set. If successful (e.g. map has
   * capacity) then put returns a pointer to the value object, which
   * the caller can then write, Returns {nullptr, false} if the key
   * was too large, or the capacity of the map has been exceeded.
   *
   * If the value was already present in the map, then {value, false} is returned.
   * The caller may need to clean up an old value.
   *
   * If the value is newly allocated, then {value, true} is returned.
   *
   * @return a pair with the value-pointer (or nullptr), and a bool indicating
   *         whether the value is newly allocated.
   */
  ValueCreatedPair insert(absl::string_view key) {
    Value* value = get(key);
    if (value != nullptr) {
      return ValueCreatedPair(value, false);
    }
    if (control_->size >= options_.capacity) {
      return ValueCreatedPair(nullptr, false);
    }
    const uint32_t slot = computeSlot(key);
    const uint32_t cell_index = control_->free_cell_index;
    Cell& cell = getCell(cell_index);
    control_->free_cell_index = cell.next_cell;
    cell.next_cell = slots_[slot];
    slots_[slot] = cell_index;
    value = &cell.value;
    value->initialize(key);
    ++control_->size;
    return ValueCreatedPair(value, true);
  }

  /**
   * Removes the specified key from the map, returning bool if the key
   * was found.
   * @param key the key to remove
   */
  bool remove(absl::string_view key) {
    const uint32_t slot = computeSlot(key);
    uint32_t* next = nullptr;
    for (uint32_t* cptr = &slots_[slot]; *cptr != Sentinal; cptr = next) {
      Cell& cell = getCell(*cptr);
      next = &cell.next_cell;
      if (cell.value.key() == key) {
        control_->free_cell_index = *cptr;
        --control_->size;
        *cptr = *next; // Splices current cell out of slot-chain.
        *next = control_->free_cell_index;
        return true;
      }
    }
    return false;
  }

  /** Returns the number of key/values stored in the map. */
  uint32_t size() const { return control_->size; }

  /**
   * Gets the value associated with a key, returning nullptr if the value was not found.
   * @param key
   */
  Value* get(absl::string_view key) {
    const uint32_t slot = computeSlot(key);
    for (uint32_t c = slots_[slot]; c != Sentinal; c = getCell(c).next_cell) {
      Cell& cell = getCell(c);
      if (cell.value.key() == key) {
        return &cell.value;
      }
    }
    return nullptr;
  }

private:
  uint32_t computeSlot(absl::string_view key) {
    return HashUtil::xxHash64(key) % options_.num_slots;
  }

  /**
   * Represents control-values for the hash-table, including a mutex, which
   * must gate all access to the internals.
   */
  struct Control {
    std::string toString() const {
      return fmt::format("{} size={} free_cell_index={}", options.toString(), size,
                         free_cell_index);
    }

    SharedMemoryHashSetOptions options; // Options established at map construction time.
    uint32_t num_bytes;                 // Bytes allocated on behalf of the map.
    uint32_t size;                      // Number of values currently stored.
    uint32_t free_cell_index;           // Offset of first free cell.
  };

  /**
   * Represents a value-cell, which is stored in a linked-list from each slot.
   */
  struct Cell {
    uint32_t next_cell; // Offset of next cell in map->cells_, terminated with Sentinal.
    Value value;        // Templated value field.
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
  static uint32_t cellOffset(uint32_t cell_index) {
    // sizeof(Cell) includes 'sizeof Value' which may not be accurate. So we need to
    // subtract that off, and add the template method's view of the actual value-size.
    uint32_t cell_size = align(sizeof(Cell) + Value::size() - sizeof(Value));
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
  }

  // Copy of the options in process-local memory; which is used to help compute
  // the required size in bytes after construction and before init/attach.
  const SharedMemoryHashSetOptions options_;

  // Pointers into shared memory. Cells go first, because Value may need a more aggressive
  // aligmnment.
  Cell* cells_;
  Control* control_;
  uint32_t* slots_;
};

} // namespace Envoy
