#!/usr/bin/python

# Main library file containing all the composite deserializer logic.


def generate_main_code(serialization_composite_h_file):
  """
  Main code generator.
  Renders the header file for serialization composites.
  The location of output file is provided as argument.
  """
  generate_code('serialization_composite_h.j2', serialization_composite_h_file)


def generate_test_code(serialization_composite_test_cc_file):
  """
  Test code generator.
  Renders the test file for serialization composites.
  The location of output file is provided as argument.
  """
  generate_code('serialization_composite_test_cc.j2', serialization_composite_test_cc_file)


def generate_code(template_name, output_file):
  """
  Gets definition of structures to render.
  Then renders these structures using template provided into provided output file.
  """
  field_counts = get_field_counts()
  template = RenderingHelper.get_template(template_name)
  contents = template.render(counts=field_counts)
  with open(output_file, 'w') as fd:
    fd.write(contents)


def get_field_counts():
  """
  Generate argument counts that should be processed by composite deserializers.
  """
  return range(1, 12)


class RenderingHelper:
  """
  Helper for jinja templates.
  """

  @staticmethod
  def get_template(template):
    import jinja2
    import os
    import sys
    # Templates are resolved relatively to main start script, due to main & test templates being
    # stored in different directories.
    env = jinja2.Environment(loader=jinja2.FileSystemLoader(
        searchpath=os.path.dirname(os.path.abspath(sys.argv[0]))))
    return env.get_template(template)
