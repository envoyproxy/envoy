// NOLINT(namespace-envoy)

#include <unistd.h>

#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>

#include "v8-version.h"
#include "wasm-api/wasm.hh"

uint32_t parseVarint(const byte_t*& pos, const byte_t* end) {
  uint32_t n = 0;
  uint32_t shift = 0;
  byte_t b;

  do {
    if (pos + 1 > end) {
      return static_cast<uint32_t>(-1);
    }
    b = *pos++;
    n += (b & 0x7f) << shift;
    shift += 7;
  } while ((b & 0x80) != 0);

  return n;
}

wasm::vec<byte_t> getVarint(uint32_t value) {
  byte_t bytes[5];
  int pos = 0;

  while (pos < 5) {
    if ((value & ~0x7F) == 0) {
      bytes[pos++] = static_cast<uint8_t>(value);
      break;
    }

    bytes[pos++] = static_cast<uint8_t>(value & 0x7F) | 0x80;
    value >>= 7;
  }

  auto vec = wasm::vec<byte_t>::make_uninitialized(pos);
  ::memcpy(vec.get(), bytes, pos);

  return vec;
}

wasm::vec<byte_t> readWasmModule(const char* path, const std::string& name) {
  // Open binary file.
  auto file = std::ifstream(path, std::ios::binary);
  file.seekg(0, std::ios_base::end);
  const auto size = file.tellg();
  file.seekg(0);
  auto content = wasm::vec<byte_t>::make_uninitialized(size);
  file.read(content.get(), size);
  file.close();

  if (file.fail()) {
    std::cerr << "ERROR: Failed to read the input file from: " << path << std::endl;
    return wasm::vec<byte_t>::invalid();
  }

  // Wasm header is 8 bytes (magic number + version).
  const uint8_t magic_number[4] = {0x00, 0x61, 0x73, 0x6d};
  if (size < 8 || ::memcmp(content.get(), magic_number, 4) != 0) {
    std::cerr << "ERROR: Failed to parse corrupted Wasm module from: " << path << std::endl;
    return wasm::vec<byte_t>::invalid();
  }

  // Parse custom sections to see if precompiled module already exists.
  const byte_t* pos = content.get() + 8 /* Wasm header */;
  const byte_t* end = content.get() + content.size();
  while (pos < end) {
    if (pos + 1 > end) {
      std::cerr << "ERROR: Failed to parse corrupted Wasm module from: " << path << std::endl;
      return wasm::vec<byte_t>::invalid();
    }
    const auto section_type = *pos++;
    const auto section_len = parseVarint(pos, end);
    if (section_len == static_cast<uint32_t>(-1) || pos + section_len > end) {
      std::cerr << "ERROR: Failed to parse corrupted Wasm module from: " << path << std::endl;
      return wasm::vec<byte_t>::invalid();
    }
    if (section_type == 0 /* custom section */) {
      const auto section_data_start = pos;
      const auto section_name_len = parseVarint(pos, end);
      if (section_name_len == static_cast<uint32_t>(-1) || pos + section_name_len > end) {
        std::cerr << "ERROR: Failed to parse corrupted Wasm module from: " << path << std::endl;
        return wasm::vec<byte_t>::invalid();
      }
      if (section_name_len == name.size() && ::memcmp(pos, name.data(), section_name_len) == 0) {
        std::cerr << "ERROR: Wasm module: " << path << " already contains precompiled module."
                  << std::endl;
        return wasm::vec<byte_t>::invalid();
      }
      pos = section_data_start + section_len;
    } else {
      pos += section_len;
    }
  }

  return content;
}

wasm::vec<byte_t> stripWasmModule(const wasm::vec<byte_t>& module) {
  std::vector<byte_t> stripped;

  const byte_t* pos = module.get();
  const byte_t* end = module.get() + module.size();

  // Copy Wasm header.
  stripped.insert(stripped.end(), pos, pos + 8);
  pos += 8;

  while (pos < end) {
    const auto section_start = pos;
    if (pos + 1 > end) {
      std::cerr << "ERROR: Failed to parse corrupted Wasm module." << std::endl;
      return wasm::vec<byte_t>::invalid();
    }
    const auto section_type = *pos++;
    const auto section_len = parseVarint(pos, end);
    if (section_len == static_cast<uint32_t>(-1) || pos + section_len > end) {
      std::cerr << "ERROR: Failed to parse corrupted Wasm module." << std::endl;
      return wasm::vec<byte_t>::invalid();
    }
    if (section_type != 0 /* custom section */) {
      stripped.insert(stripped.end(), section_start, pos + section_len);
    }
    pos += section_len;
  }

  return wasm::vec<byte_t>::make(stripped.size(), stripped.data());
}

wasm::vec<byte_t> serializeWasmModule(const char* path, const wasm::vec<byte_t>& content) {
  const auto engine = wasm::Engine::make();
  if (engine == nullptr) {
    std::cerr << "ERROR: Failed to start V8." << std::endl;
    return wasm::vec<byte_t>::invalid();
  }

  const auto store = wasm::Store::make(engine.get());
  if (store == nullptr) {
    std::cerr << "ERROR: Failed to create V8 isolate." << std::endl;
    return wasm::vec<byte_t>::invalid();
  }

  const auto module = wasm::Module::make(store.get(), content);
  if (module == nullptr) {
    std::cerr << "ERROR: Failed to instantiate WebAssembly module from: " << path << std::endl;
    return wasm::vec<byte_t>::invalid();
  }

  // TODO(PiotrSikora): figure out how to hook the completion callback.
  sleep(3);

  return module->serialize();
}

bool writeWasmModule(const char* path, const wasm::vec<byte_t>& module, size_t stripped_module_size,
                     const std::string& section_name, const wasm::vec<byte_t>& serialized) {
  auto file = std::fstream(path, std::ios::out | std::ios::binary);
  file.write(module.get(), module.size());
  const char section_type = '\0'; // custom section
  file.write(&section_type, 1);
  const auto section_name_len = getVarint(section_name.size());
  const auto section_size =
      getVarint(section_name_len.size() + section_name.size() + serialized.size());
  file.write(section_size.get(), section_size.size());
  file.write(section_name_len.get(), section_name_len.size());
  file.write(section_name.data(), section_name.size());
  file.write(serialized.get(), serialized.size());
  file.close();

  if (file.fail()) {
    std::cerr << "ERROR: Failed to write the output file to: " << path << std::endl;
    return false;
  }

  const auto total_size = module.size() + 1 + section_size.size() + section_name_len.size() +
                          section_name.size() + serialized.size();
  std::cout << "Written " << total_size << " bytes (bytecode: " << stripped_module_size << " bytes,"
            << " precompiled: " << serialized.size() << " bytes)." << std::endl;
  return true;
}

#if defined(__linux__) && defined(__x86_64__)
#define WEE8_PLATFORM "linux_x86_64"
#else
#define WEE8_PLATFORM ""
#endif

int main(int argc, char* argv[]) {
  if (sizeof(WEE8_PLATFORM) - 1 == 0) {
    std::cerr << "Unsupported platform." << std::endl;
    return EXIT_FAILURE;
  }

  if (argc != 3) {
    std::cerr << "Usage: " << argv[0] << " <input> <output>" << std::endl;
    return EXIT_FAILURE;
  }

  const std::string section_name = "precompiled_wee8_v" + std::to_string(V8_MAJOR_VERSION) + "." +
                                   std::to_string(V8_MINOR_VERSION) + "." +
                                   std::to_string(V8_BUILD_NUMBER) + "." +
                                   std::to_string(V8_PATCH_LEVEL) + "_" + WEE8_PLATFORM;

  const auto module = readWasmModule(argv[1], section_name);
  if (!module) {
    return EXIT_FAILURE;
  }

  const auto stripped_module = stripWasmModule(module);
  if (!stripped_module) {
    return EXIT_FAILURE;
  }

  const auto serialized = serializeWasmModule(argv[1], stripped_module);
  if (!serialized) {
    return EXIT_FAILURE;
  }

  if (!writeWasmModule(argv[2], module, stripped_module.size(), section_name, serialized)) {
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
