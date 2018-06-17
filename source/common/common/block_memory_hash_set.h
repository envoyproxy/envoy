#pragma once

#include <algorithm>
#include <string>
#include <utility>

#include "envoy/common/exception.h"

#include "common/common/assert.h"
#include "common/common/fmt.h"
#include "common/common/logger.h"

#include "absl/strings/string_view.h"

namespace Envoy {

/**
 * Initialization parameters for BlockMemoryHashSet. The options are duplicated
 * to the control-block after init, to aid with sanity checking when attaching
 * an existing memory segment.
 */
struct BlockMemoryHashSetOptions {
  std::string toString() const {
    return fmt::format("capacity={}, num_slots={}", capacity, num_slots);
  }
  bool operator==(const BlockMemoryHashSetOptions& that) const {
    return capacity == that.capacity && num_slots == that.num_slots;
  }
  bool operator!=(const BlockMemoryHashSetOptions& that) const { return !(*this == that); }

  uint32_t capacity;  // how many values can be stored.
  uint32_t num_slots; // determines speed of hash vs size efficiency.
};

/**
 * Implements hash_set<Value> without using pointers, suitable for use
 * in shared memory. Users must commit to capacity and num_slots at
 * construction time. Value must provide these methods:
 *    absl::string_view Value::key()
 *    void Value::initialize(absl::string_view key)
 *    static uint64_t Value::size()
 *    static uint64_t Value::hash()
 *
 * This set may also be suitable for persisting a hash-table to long
 * term storage, but not across machine architectures, as it doesn't
 * use network byte order for storing ints.
 */
template <class Value> class BlockMemoryHashSet : public Logger::Loggable<Logger::Id::config> {
public:
  /**
   * Sentinal used for next_cell links to indicate end-of-list.
   */
  static const uint32_t Sentinal = 0xffffffff;

  /** Type used by put() to indicate the value at a key, and whether it was created */
  typedef std::pair<Value*, bool> ValueCreatedPair;

  /**
   * Constructs a map control structure given a set of options, which cannot be changed.
   * @param options describes the parameters comtrolling set layout.
   * @param init true if the memory should be initialized on construction. If false,
   *             the data in the table will be sanity checked, and an exception thrown if
   *             it is incoherent or mismatches the passed-in options.
   * @param memory the memory buffer for the set data.
   *
   * Note that no locking of any kind is done by this class; this must be done at the
   * call-site to support concurrent access.
   */
  BlockMemoryHashSet(const BlockMemoryHashSetOptions& options, bool init, uint8_t* memory)
      : cells_(nullptr), control_(nullptr), slots_(nullptr) {
    mapMemorySegments(options, memory);
    if (init) {
      initialize(options);
    } else if (!attach(options)) {
      throw EnvoyException("BlockMemoryHashSet: Incompatible memory block");
    }
  }

  /**
   * Returns the numbers of byte required for the hash-table, based on
   * the control structure. This must be used to allocate the
   * backing-store (eg) in memory, which we do after
   * constructing the object with the desired sizing.
   */
  static uint64_t numBytes(const BlockMemoryHashSetOptions& options) {
    uint64_t size =
        cellOffset(options.capacity) + sizeof(Control) + options.num_slots * sizeof(uint32_t);
    return align(size);
  }

  uint64_t numBytes() const { return numBytes(control_->options); }

  /**
   * Returns the options structure that was used to construct the set.
   */
  const BlockMemoryHashSetOptions& options() const { return control_->options; }

  /** Examines the data structures to see if they are sane, assert-failing on any trouble. */
  void sanityCheck() {
    RELEASE_ASSERT(control_->size <= control_->options.capacity);

    // As a sanity check, make sure there are control_->size values
    // reachable from the slots, each of which has a valid
    // char_offset.
    //
    // Avoid infinite loops if there is a next_cell_index cycle within a
    // slot. Note that the num_values message will be emitted outside
    // the loop.
    uint32_t num_values = 0;
    for (uint32_t slot = 0; slot < control_->options.num_slots; ++slot) {
      uint32_t next = 0; // initialized to silence compilers.
      for (uint32_t cell_index = slots_[slot];
           (cell_index != Sentinal) && (num_values <= control_->size); cell_index = next) {
        RELEASE_ASSERT(cell_index < control_->options.capacity);
        Cell& cell = getCell(cell_index);
        absl::string_view key = cell.value.key();
        RELEASE_ASSERT(computeSlot(key) == slot);
        next = cell.next_cell_index;
        ++num_values;
      }
    }
    RELEASE_ASSERT(num_values == control_->size);

    uint32_t num_free_entries = 0;
    uint32_t expected_free_entries = control_->options.capacity - control_->size;

    // Don't infinite-loop with a corruption; break when we see there's a problem.
    for (uint32_t cell_index = control_->free_cell_index;
         (cell_index != Sentinal) && (num_free_entries <= expected_free_entries);
         cell_index = getCell(cell_index).next_cell_index) {
      ++num_free_entries;
    }
    RELEASE_ASSERT(num_free_entries == expected_free_entries);
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
    if (control_->size >= control_->options.capacity) {
      return ValueCreatedPair(nullptr, false);
    }
    const uint32_t slot = computeSlot(key);
    const uint32_t cell_index = control_->free_cell_index;
    Cell& cell = getCell(cell_index);
    control_->free_cell_index = cell.next_cell_index;
    cell.next_cell_index = slots_[slot];
    slots_[slot] = cell_index;
    value = &cell.value;
    value->initialize(key);
    ++control_->size;
    return ValueCreatedPair(value, true);
  }

  /**
   * Removes the specified key from the map, returning true if the key
   * was found.
   * @param key the key to remove
   */
  bool remove(absl::string_view key) {
    const uint32_t slot = computeSlot(key);
    uint32_t* next = nullptr;
    for (uint32_t* cptr = &slots_[slot]; *cptr != Sentinal; cptr = next) {
      const uint32_t cell_index = *cptr;
      Cell& cell = getCell(cell_index);
      if (cell.value.key() == key) {
        // Splice current cell out of slot-chain.
        *cptr = cell.next_cell_index;

        // Splice current cell into free-list.
        cell.next_cell_index = control_->free_cell_index;
        control_->free_cell_index = cell_index;

        --control_->size;
        return true;
      }
      next = &cell.next_cell_index;
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
    for (uint32_t c = slots_[slot]; c != Sentinal; c = getCell(c).next_cell_index) {
      Cell& cell = getCell(c);
      if (cell.value.key() == key) {
        return &cell.value;
      }
    }
    return nullptr;
  }

  /**
   * Computes a version signature based on the options and the hash function.
   */
  std::string version() {
    return fmt::format("options={} hash={} size={}", control_->options.toString(),
                       control_->hash_signature, numBytes());
  }

private:
  friend class BlockMemoryHashSetTest;

  /**
   * Initializes a hash-map on raw memory. No expectations are made about the state of the memory
   * coming in.
   * @param memory
   */
  void initialize(const BlockMemoryHashSetOptions& options) {
    control_->hash_signature = Value::hash(signatureStringToHash());
    control_->num_bytes = numBytes(options);
    control_->options = options;
    control_->size = 0;
    control_->free_cell_index = 0;

    // Initialize all the slots;
    for (uint32_t slot = 0; slot < options.num_slots; ++slot) {
      slots_[slot] = Sentinal;
    }

    // Initialize the free-cell list.
    const uint32_t last_cell = options.capacity - 1;
    for (uint32_t cell_index = 0; cell_index < last_cell; ++cell_index) {
      Cell& cell = getCell(cell_index);
      cell.next_cell_index = cell_index + 1;
    }
    getCell(last_cell).next_cell_index = Sentinal;
  }

  /**
   * Attempts to attach to an existing memory segment. Does a (relatively) quick
   * sanity check to make sure the options copied to the provided memory match, and also
   * that the slot, cell, and key-string structures look sane.
   */
  bool attach(const BlockMemoryHashSetOptions& options) {
    if (numBytes(options) != control_->num_bytes) {
      ENVOY_LOG(error, "BlockMemoryHashSet unexpected memory size {} != {}", numBytes(options),
                control_->num_bytes);
      return false;
    }
    if (Value::hash(signatureStringToHash()) != control_->hash_signature) {
      ENVOY_LOG(error, "BlockMemoryHashSet hash signature mismatch.");
      return false;
    }
    sanityCheck();
    return true;
  }

  uint32_t computeSlot(absl::string_view key) {
    return Value::hash(key) % control_->options.num_slots;
  }

  /**
   * Computes a signature string, composed of all the non-zero 8-bit characters.
   * This is used for detecting if the hash algorithm changes, which invalidates
   * any saved stats-set.
   */
  static std::string signatureStringToHash() {
    std::string signature_string;
    signature_string.resize(255);
    for (int i = 1; i <= 255; ++i) {
      signature_string[i - 1] = i;
    }
    return signature_string;
  }

  /**
   * Represents control-values for the hash-table.
   */
  struct Control {
    BlockMemoryHashSetOptions options; // Options established at map construction time.
    uint64_t hash_signature;           // Hash of a constant signature string.
    uint64_t num_bytes;                // Bytes allocated on behalf of the map.
    uint32_t size;                     // Number of values currently stored.
    uint32_t free_cell_index;          // Offset of first free cell.
  };

  /**
   * Represents a value-cell, which is stored in a linked-list from each slot.
   */
  struct Cell {
    uint32_t next_cell_index; // Index of next cell in map->cells_, terminated with Sentinal.
    Value value;              // Templated value field.
  };

  // It seems like this is an obvious constexpr, but it won't compile as one.
  static uint64_t calculateAlignment() {
    return std::max(alignof(Cell), std::max(alignof(uint32_t), alignof(Control)));
  }

  static uint64_t align(uint64_t size) {
    const uint64_t alignment = calculateAlignment();
    // Check that alignment is a power of 2:
    // http://www.graphics.stanford.edu/~seander/bithacks.html#DetermineIfPowerOf2
    RELEASE_ASSERT((alignment > 0) && ((alignment & (alignment - 1)) == 0));
    return (size + alignment - 1) & ~(alignment - 1);
  }

  /**
   * Computes the byte offset of a cell into cells_. This is not
   * simply an array index because we don't know the size of a key at
   * compile-time.
   */
  static uint64_t cellOffset(uint32_t cell_index) {
    // sizeof(Cell) includes 'sizeof Value' which may not be accurate. So we need to
    // subtract that off, and add the template method's view of the actual value-size.
    uint64_t cell_size = align(sizeof(Cell) + Value::size() - sizeof(Value));
    return cell_index * cell_size;
  }

  /**
   * Returns a reference to a Cell at the specified index.
   */
  Cell& getCell(uint32_t cell_index) {
    // Because the key-size is parameteriziable, an array-lookup on sizeof(Cell) does not work.
    char* ptr = reinterpret_cast<char*>(cells_) + cellOffset(cell_index);
    RELEASE_ASSERT((reinterpret_cast<uint64_t>(ptr) & (calculateAlignment() - 1)) == 0);
    return *reinterpret_cast<Cell*>(ptr);
  }

  /** Maps out the segments of memory for us to work with. */
  void mapMemorySegments(const BlockMemoryHashSetOptions& options, uint8_t* memory) {
    // Note that we are not examining or mutating memory here, just looking at the pointer,
    // so we don't need to hold any locks.
    cells_ = reinterpret_cast<Cell*>(memory); // First because Value may need to be aligned.
    memory += cellOffset(options.capacity);
    control_ = reinterpret_cast<Control*>(memory);
    memory += sizeof(Control);
    slots_ = reinterpret_cast<uint32_t*>(memory);
  }

  // Pointers into memory. Cells go first, because Value may need a more aggressive
  // aligmnment.
  Cell* cells_;
  Control* control_;
  uint32_t* slots_;
};

} // namespace Envoy
