from typing import List
from docutils import nodes
from docutils.parsers.rst import Directive
from docutils.parsers.rst import directives
from sphinx.application import Sphinx
from sphinx.util.docutils import SphinxDirective
from sphinx.directives.code import CodeBlock
from sphinx.errors import ExtensionError

import os
import subprocess


class ValidatingCodeBlock(CodeBlock):
  """A directive that provides protobuf yaml formatting and validation.

    'type-name' option is required and expected to conain full Envoy API type.
    An ExtensionError is raised on validation failure.
    Validation will be skipped if SPHINX_SKIP_CONFIG_VALIDATION environment variable is set.
    """
  has_content = True
  required_arguments = CodeBlock.required_arguments
  optional_arguments = CodeBlock.optional_arguments
  final_argument_whitespace = CodeBlock.final_argument_whitespace
  option_spec = {
      'type-name': directives.unchanged,
  }
  option_spec.update(CodeBlock.option_spec)
  skip_validation = (os.getenv('SPHINX_SKIP_CONFIG_VALIDATION') or 'false').lower() == 'true'

  def run(self):
    source, line = self.state_machine.get_source_and_line(self.lineno)
    # built-in directives.unchanged_required option validator produces a confusing error message
    if self.options.get('type-name') == None:
      raise ExtensionError("Expected type name in: {0} line: {1}".format(source, line))

    if not ValidatingCodeBlock.skip_validation:
      args = [
          'bazel-bin/tools/config_validation/validate_fragment',
          self.options.get('type-name'), '-s', '\n'.join(self.content)
      ]
      completed = subprocess.run(args,
                                 stdout=subprocess.PIPE,
                                 stderr=subprocess.PIPE,
                                 encoding='utf-8')
      if completed.returncode != 0:
        raise ExtensionError(
            "Failed config validation for type: '{0}' in: {1} line: {2}:\n {3}".format(
                self.options.get('type-name'), source, line, completed.stderr))

    self.options.pop('type-name', None)
    return list(CodeBlock.run(self))


def setup(app):
  app.add_directive("validated-code-block", ValidatingCodeBlock)

  return {
      'version': '0.1',
      'parallel_read_safe': True,
      'parallel_write_safe': True,
  }
