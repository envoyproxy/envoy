"""Generates the Windows module-definition file for dynamic module host callbacks.

The Windows linker rejects a .def that names an undefined symbol and has no wildcard export. The
callbacks defined in abi_impl.cc are present in any binary that links the base abi_impl target, so
deriving the list from that file keeps it in sync with the ABI and keeps every exported name
resolvable. The names are emitted undecorated, which matches Envoy's x64-only Windows target.
"""

import re
import sys

# Matches a callback definition: the name followed by an argument list and an opening brace.
# Anchoring on the definition keeps references in comments, strings, or calls out of the export
# list, where a name that is not defined in the binary would break the Windows link.
_CALLBACK_DEFINITION = re.compile(
    r"(?<![A-Za-z0-9_])(envoy_dynamic_module_callback_[a-z0-9_]+)\s*\([^{};]*\)\s*\{")


def generate(abi_impl_source):
    """Returns the module-definition contents exporting the callbacks defined in the source."""
    names = sorted(set(_CALLBACK_DEFINITION.findall(abi_impl_source)))
    return "EXPORTS\n" + "".join("  {}\n".format(name) for name in names)


if __name__ == "__main__":
    input_path, output_path = sys.argv[1], sys.argv[2]
    with open(input_path, encoding="utf-8") as input_file:
        contents = input_file.read()
    with open(output_path, "w", encoding="utf-8") as output_file:
        output_file.write(generate(contents))
