#!/usr/bin/python

# Launcher for generating composite serializer tests.

import contrib.kafka.filters.network.source.serialization.generator as generator
import sys
import os


def main():
    """
  Serialization composite test generator
  ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
  Generates test source files for composite deserializers.
  The files are generated, as they are extremely repetitive (tests for composite deserializer
  for 0..9 sub-deserializers).

  Usage:
    launcher.py LOCATION_OF_OUTPUT_FILE
  where:
  LOCATION_OF_OUTPUT_FILE : location of 'serialization_composite_test.cc'.

  Creates 'serialization_composite_test.cc' - tests composite deserializers.

  Template used is 'serialization_composite_test_cc.j2'.
  """
    serialization_composite_test_cc_file = os.path.abspath(sys.argv[1])
    generator.generate_test_code(serialization_composite_test_cc_file)


if __name__ == "__main__":
    main()
