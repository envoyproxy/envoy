// NOLINT(namespace-envoy)

#include <chrono>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <thread>
#include <vector>

#include "absl/strings/string_view.h"
#include "include/proxy-wasm/bytecode_util.h"
#include "include/proxy-wasm/pairs_util.h"
#include "include/proxy-wasm/sdk.h"
#include "include/proxy-wasm/v8.h"
#include "include/proxy-wasm/wasm_vm.h"

std::string readWasmModule(const char* path) {
  // Open binary file.
  auto file = std::ifstream(path, std::ios::binary);
  auto content =
      std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
  file.close();

  if (file.fail()) {
    std::cerr << "ERROR: Failed to read the input file from: " << path << std::endl;
    return "";
  }

  return content;
}

bool writeWasmModule(absl::string_view module, const char* path) {
  auto file = std::fstream(path, std::ios::out | std::ios::binary);
  file.write(module.data(), module.size());
  file.close();

  if (file.fail()) {
    std::cerr << "ERROR: Failed to write the output file to: " << path << std::endl;
    return false;
  }

  std::cout << "Written " << module.size() << " bytes." << std::endl;
  return true;
}

int main(int argc, char* argv[]) {
  if (argc != 3) {
    std::cerr << "Usage: " << argv[0] << " <input> <output>" << std::endl;
    return EXIT_FAILURE;
  }

  const std::string module = readWasmModule(argv[1]);
  if (module.empty()) {
    return EXIT_FAILURE;
  }

  std::string stripped_module;
  if (!proxy_wasm::BytecodeUtil::getStrippedSource(module, stripped_module)) {
    return EXIT_FAILURE;
  }

  std::unique_ptr<proxy_wasm::WasmVm> vm = proxy_wasm::createV8Vm();
  if (vm == nullptr) {
    return EXIT_FAILURE;
  }

  if (!vm->load(stripped_module, "", {})) {
    return EXIT_FAILURE;
  }

  std::optional<std::string> serialized_module = vm->serialize(stripped_module);
  if (!serialized_module) {
    return EXIT_FAILURE;
  }

  if (!writeWasmModule(*serialized_module, argv[2])) {
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
