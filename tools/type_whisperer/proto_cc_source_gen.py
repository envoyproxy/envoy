# Generate a C++ source file with given text protos embedded.

import pathlib
import string
import sys

CC_SOURCE_TEMPLATE = string.Template("""namespace Envoy {
namespace Tools {
namespace TypeWhisperer {

const char* $constant = R"EOF($pb_text)EOF";

}
}
}
""")

if __name__ == '__main__':
  constant_name = sys.argv[1]
  output_path = sys.argv[2]
  input_paths = sys.argv[3:]
  pb_text = '\n'.join(pathlib.Path(path).read_text() for path in input_paths)
  with open(output_path, 'w') as f:
    f.write(CC_SOURCE_TEMPLATE.substitute(constant=constant_name, pb_text=pb_text))
