#!/usr/bin/python

# Launcher for generating composite serializer code.

import contrib.kafka.filters.network.source.serialization.generator as generator
import sys
import os


def main():
    """
  Serialization composite code generator
  ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
  Generates main source code files for composite deserializers.
  The files are generated, as they are extremely repetitive (composite deserializer for 0..9
  sub-deserializers).

  Usage:
    launcher.py LOCATION_OF_OUTPUT_FILE
  where:
  LOCATION_OF_OUTPUT_FILE : location of 'serialization_composite.h'.

  Creates 'serialization_composite.h' - header with declarations of
  CompositeDeserializerWith???Delegates classes.

  Template used: 'serialization_composite_h.j2'.
  """
    serialization_composite_h_file = os.path.abspath(sys.argv[1])
    generator.generate_main_code(serialization_composite_h_file)


if __name__ == "__main__":
    main()
