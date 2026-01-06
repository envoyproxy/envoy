import hashlib
import sys

# Copyright 2025 Envoy Project Authors
# Use of this source code is governed by an Apache-2.0 style license that can be found in the LICENSE file.


def main():
    if len(sys.argv) != 3:
        print("Usage: compute_abi_hash.py <input_abi_header> <output_abi_version_header>")
        sys.exit(1)

    input_path = sys.argv[1]
    output_path = sys.argv[2]

    with open(input_path, 'rb') as f:
        digest = hashlib.sha256(f.read()).hexdigest()

    content = f"""#pragma once
#ifdef __cplusplus
namespace Envoy {{
namespace Extensions {{
namespace DynamicModules {{
#endif
// This is the ABI version calculated as a sha256 hash of the ABI header files. When the ABI
// changes, this value must change, and the correctness of this value is checked by the test.
const char* kAbiVersion = "{digest}";

#ifdef __cplusplus
}} // namespace DynamicModules
}} // namespace Extensions
}} // namespace Envoy
#endif
"""
    with open(output_path, 'w') as f:
        f.write(content)


if __name__ == "__main__":
    main()
