# Generate a text proto from a given list of FileDescriptorSets.
# TODO(htuch): switch to base64 encoded binary output in the future,
# this will avoid needing to deal with option preserving imports below.

import sys

from google.protobuf import descriptor_pb2

# Needed to avoid annotation option stripping during pb_text generation.
from udpa.annotations import migrate_pb2


def Decode(path):
  with open(path, 'rb') as f:
    file_set = descriptor_pb2.FileDescriptorSet()
    file_set.ParseFromString(f.read())
    return str(file_set)


if __name__ == '__main__':
  output_path = sys.argv[1]
  input_paths = sys.argv[2:]
  pb_text = '\n'.join(Decode(path) for path in input_paths)
  with open(output_path, 'w') as f:
    f.write(pb_text)
