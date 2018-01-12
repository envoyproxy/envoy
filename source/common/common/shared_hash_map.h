#pragma once

#include <pthread.h>

#include <string>

#include "common/common/hash.h"
#include "common/common/logger.h"

#include "absl/strings/string_view.h"
#include "fmt/format.h"

namespace Envoy {

// This block is used solely to help with initialization. It is duplicated
// to the control-block after init.
struct SharedHashMapOptions {
  std::string toString() {
    return fmt::format("capacity={}, num_string_bytes={}, num_slots={}", capacity, num_string_bytes,
                       num_slots);
  }

  uint32_t capacity;         // how many values can be stored.
  uint32_t num_string_bytes; // how many bytes of string can be stored.
  uint32_t num_slots;        // determines speed of hash vs size efficiency.
};

// Hash-map with no pointers, suitable for use in shared memory. This hash-table
// exploits a simplification for its intended use-case; it provides no way to delete a
// key.
template <class Value> class SharedHashMap {
public:
  // Sentinal to used for a cell's char_offset to indicate it's free.
  // We also use this sentinal to denote the end of the free-list of cells.
  static const uint32_t FreeCell = 0xffffffff;

  struct Control {
    std::string toString() {
      return fmt::format("{} size={} next_key_char={}", options.toString(), size, next_key_char);
    }

    pthread_mutex_t mutex;
    SharedHashMapOptions options;
    uint32_t size;
    uint32_t next_key_char;
  };

  // For sanity, debugging, and slightly few casts, the shared-memory layout is divided into
  // four sections: control-block, strings, slots, and payload. These boundaries are for now
  // fixed at init-time. The maximum size for a string is 256 bytes, as we will use byte 0
  // to contain the size.

  struct Cell {
    void free() { char_offset = FreeCell; }

    uint32_t char_offset;
    uint32_t next_cell;
    Value value;
  };

  SharedHashMap(const SharedHashMapOptions& options)
      : options_(options), control_(nullptr), slots_(nullptr), cells_(nullptr), chars_(nullptr) {}

  size_t numBytes() const {
    return sizeof(Control) + (options_.num_slots * sizeof(uint32_t)) +
           (options_.capacity * sizeof(Cell)) + options_.num_string_bytes;
  }

  // Attempts to attach to an existing shared memory segment. In
  // particular, do a quick sanity check on the sizes of the
  bool attach(uint8_t* memory) {
    initHelper(memory);
    lock(); // might hang if program previously crashed.
    bool ret = sanityCheck();
    unlock();
    return ret;
  }

  bool sanityCheck() {
    bool ret = true;
    if (memcmp(&options_, &control_->options, sizeof(SharedHashMapOptions)) != 0) {
      // options doesn't match.
      // ENVOY_LOG(error, "SharedMap options don't match");
      return false;
    }

    // As a sanity check, makee sure there are control_->size values
    // reachable from the slots, each of which has a valid char_offset
    uint32_t num_values = 0;
    for (uint32_t i = 0; i < options_.num_slots; ++i) {
      for (uint32_t j = slots_[i]; j != FreeCell; j = cells_[j].next_cell) {
        uint32_t char_offset = cells_[j].char_offset;
        if (char_offset == FreeCell) {
          // ENVOY_LOG(error, "SharedMap live cell has char_offset==FreeCell");
          ret = false;
        } else if (char_offset >= options_.num_string_bytes) {
          // ENVOY_LOG(error, "SharedMap live cell has corrupt_offset: {}", char_offset);
          ret = false;
        } else {
        }
        ++num_values;
      }
    }
    if (num_values != control_->size) {
      // ENVOY_LOG(error, "SharedMap has wrong number of live cells: {}, expected {}",
      //          num_values, control->size);
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
    lock();
    ret = fmt::format("options={}\ncontrol={}\n", options_.toString(), control_->toString());
    for (uint32_t i = 0; i < options_.num_slots; ++i) {
      ret += fmt::format("slot {}:", i);
      for (uint32_t j = slots_[i]; j != FreeCell; j = cells_[j].next_cell) {
        Cell* cell = cells_[j];
        uint8_t size = reinterpret_cast<uint8_t>(chars_[cell->char_offset]);
        std::string key(chars_[cell->char_offset + 1], size);
        ret += " " + key;
      }
      ret += "\n";
    }
    unlock();
    return ret;
  }

  void init(uint8_t* memory) {
    initHelper(memory);
    pthread_mutexattr_t attribute;
    pthread_mutexattr_init(&attribute);
    pthread_mutexattr_setpshared(&attribute, PTHREAD_PROCESS_SHARED);
    pthread_mutexattr_setrobust(&attribute, PTHREAD_MUTEX_ROBUST);
    pthread_mutex_init(&control_->mutex, &attribute);
    lock();

    control_->options = options_;
    control_->size = 0;
    control_->next_key_char = 0;

    // Initialize all the slots;
    for (uint32_t i = 0; i < options_.num_slots; ++i) {
      slots_[i] = FreeCell;
    }

    // Initialize all the key-char offsets.
    for (uint32_t i = 0; i < options_.capacity; ++i) {
      cells_[i].char_offset = FreeCell;
    }

    unlock();
  }

  Value* put(absl::string_view key) {
    Value* value = nullptr;
    lock();
    if ((key.size() <= 255) && (control_->size < options_.capacity) &&
        (key.size() + 1 + control_->next_key_char <= options_.num_string_bytes)) {
      uint32_t slot = HashUtil::xxHash64(key) % options_.num_slots;

      uint32_t* prevNext = &slots_[slot];
      uint32_t cell = *prevNext;

      while (cell != FreeCell) {
        prevNext = &cells_[cell].next_cell;
        cell = *prevNext;
      }

      cell = control_->size++;
      *prevNext = cell;
      value = &cells_[cell].value;
      uint32_t char_offset = control_->next_key_char;
      cells_[cell].char_offset = char_offset;
      cells_[cell].next_cell = FreeCell;
      control_->next_key_char += key.size() + 1;
      chars_[char_offset] = key.size();
      memcpy(&chars_[char_offset + 1], key.data(), key.size());
    }
    unlock();
    return value;
  }

  size_t size() const { return control_->size; }

  const Value* get(absl::string_view key) const {
    SharedHashMap* non_const_this = const_cast<SharedHashMap*>(this);
    return non_const_this->get(key);
  }

  Value* get(absl::string_view key) {
    lock();
    Value* value = nullptr;
    if (key.size() <= 255) {
      uint32_t slot = HashUtil::xxHash64(key) % options_.num_slots;
      for (uint32_t cell = slots_[slot]; cell != FreeCell; cell = cells_[cell].next_cell) {
        uint32_t char_offset = cells_[cell].char_offset;
        absl::string_view cell_key(&chars_[char_offset + 1], chars_[char_offset]);
        if (cell_key == key) {
          value = &cells_[cell].value;
          break;
        }
      }
    }
    unlock();
    return value;
  }

private:
  // Maps out the segments of shared memory for us to work with.
  void initHelper(uint8_t* memory) {
    control_ = reinterpret_cast<Control*>(memory);
    memory += sizeof(Control);
    slots_ = reinterpret_cast<uint32_t*>(memory);
    memory += options_.num_slots * sizeof(uint32_t);
    cells_ = reinterpret_cast<Cell*>(memory);
    memory += options_.capacity * sizeof(Cell);
    chars_ = reinterpret_cast<char*>(memory);
  }

  void lock() { pthread_mutex_lock(&control_->mutex); }

  void unlock() { pthread_mutex_unlock(&control_->mutex); }

  const SharedHashMapOptions options_;

  // Pointers into shared memory.
  Control* control_;
  uint32_t* slots_;
  Cell* cells_;
  char* chars_;
};

} // namespace Envoy
