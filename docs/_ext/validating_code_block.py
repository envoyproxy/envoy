from typing import List
from docutils import nodes
from docutils.parsers.rst import Directive
from docutils.parsers.rst import directives
from sphinx.application import Sphinx
from sphinx.util.docutils import SphinxDirective
from sphinx.directives.code import CodeBlock
from sphinx.errors import ExtensionError

import subprocess

class ValidatingCodeBlock(CodeBlock):

    has_content = True
    required_arguments = CodeBlock.required_arguments
    optional_arguments = CodeBlock.optional_arguments
    final_argument_whitespace = CodeBlock.final_argument_whitespace
    option_spec = {
        'type-name': directives.unchanged,
    }
    option_spec.update(CodeBlock.option_spec)

    def run(self):
        source, line = self.state_machine.get_source_and_line(self.lineno)
        # built-in directives.unchanged_required option validator produces a confusing error message
        if (self.options.get('type-name') == None):
            raise ExtensionError("Expected type name in: {0} line: {1}".format(source, line))

        process = subprocess.Popen(['bazel', 'run', '//tools/config_validation:validate_fragment', '--', self.options.get('type-name'), "-s", "\n".join(self.content)],
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE)
        stdout, stderr = process.communicate()
        if (process.poll()):
            raise ExtensionError("Failed config validation for type: '{0}' in: {1} line: {2}".format(self.options.get('type-name'), source, line))

        self.options.pop('type-name', None)
        return list(CodeBlock.run(self))

def setup(app):
    app.add_directive("validated-code-block", ValidatingCodeBlock)

    return {
        'version': '0.1',
        'parallel_read_safe': True,
        'parallel_write_safe': True,
    }
