#!/usr/bin/python

def main():
  import sys
  import os

  output_file = os.path.abspath(sys.argv[1])

  template = RenderingHelper.get_template('generated_header_h.j2')
  contents = template.render()

  with open(output_file, 'w') as fd:
    fd.write(contents)


class RenderingHelper:

  @staticmethod
  def get_template(template):
    import jinja2
    import os
    env = jinja2.Environment(
        loader=jinja2.FileSystemLoader(searchpath=os.path.dirname(os.path.abspath(__file__))))
    return env.get_template(template)


if __name__ == "__main__":
  main()
