from typing import List
from docutils import nodes
from docutils.parsers.rst import Directive
from docutils.parsers.rst import directives
from sphinx.application import Sphinx
from sphinx.util.docutils import SphinxDirective
from sphinx.directives.code import LiteralInclude 
from sphinx.errors import ExtensionError

import os
import subprocess


class ValidatingLiteralInclude(LiteralInclude):
  """Like ``.. literalinclude::``, but with protobuf validation.

    'type-name' option is required and expected to conain full Envoy API type.
    An ExtensionError is raised on validation failure.
    Validation will be skipped if SPHINX_SKIP_CONFIG_VALIDATION environment variable is set.
    """
  has_content = False
  required_arguments = LiteralInclude.required_arguments
  optional_arguments = LiteralInclude.optional_arguments
  final_argument_whitespace = LiteralInclude.final_argument_whitespace
  option_spec = {
      'type-name': directives.unchanged,
  }
  option_spec.update(LiteralInclude.option_spec)
  skip_validation = (os.getenv('SPHINX_SKIP_CONFIG_VALIDATION') or 'false').lower() == 'true'

  def run(self):
    source, line = self.state_machine.get_source_and_line(self.lineno)
    # built-in directives.unchanged_required option validator produces a confusing error message
    if self.options.get('type-name') == None:
      raise ExtensionError("Expected type name in: {0} line: {1}".format(source, line))

    if not ValidatingLiteralInclude.skip_validation:
      relative_filename, filename = self.env.relfn2path(self.arguments[0])
      args = [
          'bazel-bin/tools/config_validation/validate_fragment',
          self.options.get('type-name'), filename
      ]
      completed = subprocess.run(args,
                                 stdout=subprocess.PIPE,
                                 stderr=subprocess.PIPE,
                                 encoding='utf-8')
      if completed.returncode != 0:
        raise ExtensionError(
            "Failed config validation for type: '{0}' in: {1}:\n {2}".format(
                self.options.get('type-name'), relative_filename, completed.stderr))

    self.options.pop('type-name', None)
    return list(LiteralInclude.run(self))


def setup(app):
  app.add_directive("validatingliteralinclude", ValidatingLiteralInclude)

  return {
      'version': '0.1',
      'parallel_read_safe': True,
      'parallel_write_safe': True,
  }
