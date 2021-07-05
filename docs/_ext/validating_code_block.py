import os
import subprocess
from functools import cached_property

import yaml

from docutils.parsers.rst import directives

from sphinx.directives.code import CodeBlock
from sphinx.errors import ExtensionError


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

    @cached_property
    def configs(self) -> dict:
        _configs = dict(
            descriptor_path="",
            skip_validation=False,
            validator_path="bazel-bin/tools/config_validation/validate_fragment")
        if os.environ.get("ENVOY_DOCS_BUILD_CONFIG"):
            with open(os.environ["ENVOY_DOCS_BUILD_CONFIG"]) as f:
                _configs.update(yaml.safe_load(f.read()))
        return _configs

    @property
    def descriptor_path(self) -> str:
        return self.configs["descriptor_path"]

    @property
    def skip_validation(self) -> bool:
        return bool(self.configs["skip_validation"])

    @property
    def validator_args(self) -> tuple:
        args = (
            self.options.get('type-name'),
            '-s',
            '\n'.join(self.content),
        )
        return (
            args + ("--descriptor_path", self.descriptor_path) if self.descriptor_path else args)

    @property
    def validator_path(self) -> str:
        return self.configs["validator_path"]

    def run(self):
        source, line = self.state_machine.get_source_and_line(self.lineno)
        # built-in directives.unchanged_required option validator produces a confusing error message
        if self.options.get('type-name') == None:
            raise ExtensionError("Expected type name in: {0} line: {1}".format(source, line))

        if not self.skip_validation:
            completed = subprocess.run((self.validator_path,) + self.validator_args,
                                       stdout=subprocess.PIPE,
                                       stderr=subprocess.PIPE,
                                       encoding='utf-8')
            if completed.returncode != 0:
                raise ExtensionError(
                    "Failed config validation for type: '{0}' in: {1} line: {2}:\n {3}".format(
                        self.options.get('type-name'), source, line, completed.stderr))

        self.options.pop('type-name', None)
        return list(super().run())


def setup(app):
    app.add_directive("validated-code-block", ValidatingCodeBlock)

    return {
        'version': '0.1',
        'parallel_read_safe': True,
        'parallel_write_safe': True,
    }
