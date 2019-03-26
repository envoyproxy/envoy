#!/usr/bin/python


def main():
  """
  Serialization composite generator script
  ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
  Generates main&test source code files for composite deserializers.
  The files are generated, as they are extremely repetitive (composite deserializer for 0..9
  sub-deserializers).

  Usage:
    serialization_composite_generator.py COMMAND LOCATION_OF_OUTPUT_FILE
  where:
  COMMAND : 'generate-source', to generate source files,
            'generate-test', to generate test files.
  LOCATION_OF_OUTPUT_FILE : if generate-source: location of 'serialization_composite.h',
                            if generate-test: location of 'serialization_composite_test.cc'.

  When generating source code, it creates:
    - serialization_composite.h - header with declarations of CompositeDeserializerWith???Delegates
                                  classes.
  When generating test code, it creates:
    - serialization_composite_test.cc - tests for these classes.

  Templates used are:
  - to create 'serialization_composite.h': serialization_composite_h.j2,
  - to create 'serialization_composite_test.cc': serialization_composite_test_cc.j2.
  """

  import sys
  import os

  command = sys.argv[1]
  if 'generate-source' == command:
    serialization_composite_h_file = os.path.abspath(sys.argv[2])
  elif 'generate-test' == command:
    serialization_composite_test_cc_file = os.path.abspath(sys.argv[2])
  else:
    raise ValueError('invalid command: ' + command)

  import re
  import json

  # Number of fields deserialized by each deserializer class.
  field_counts = range(1, 10)

  # Generate main source code.
  if 'generate-source' == command:
    template = RenderingHelper.get_template('serialization_composite_h.j2')
    contents = template.render(counts=field_counts)
    with open(serialization_composite_h_file, 'w') as fd:
      fd.write(contents)

  # Generate test code.
  if 'generate-test' == command:
    template = RenderingHelper.get_template('serialization_composite_test_cc.j2')
    contents = template.render(counts=field_counts)
    with open(serialization_composite_test_cc_file, 'w') as fd:
      fd.write(contents)


class RenderingHelper:
  """
  Helper for jinja templates.
  """

  @staticmethod
  def get_template(template):
    import jinja2
    import os
    env = jinja2.Environment(
        loader=jinja2.FileSystemLoader(searchpath=os.path.dirname(os.path.abspath(__file__))))
    return env.get_template(template)


if __name__ == "__main__":
  main()
