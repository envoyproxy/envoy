"""Tests the textproto2yaml helper converting textproto
to yaml and validated code fragment.
"""
import os
import pathlib
import tempfile
import unittest

import textproto2yaml

MESSAGE_TYPE = 'xds.type.matcher.v3.Matcher'
EXPECTED_YAML = """matcher_list:
  matchers:
  - predicate:
      single_predicate:
        input:
          typed_config:
            '@type': type.googleapis.com/envoy.type.matcher.v3.HttpRequestHeaderMatchInput
            header_name: env
        value_match:
          exact: staging
    on_match:
      action:
        typed_config:
          '@type': type.googleapis.com/envoy.type.v3.TokenBucket
          max_tokens: 120
  - predicate:
      single_predicate:
        input:
          typed_config:
            '@type': type.googleapis.com/envoy.type.matcher.v3.HttpRequestHeaderMatchInput
            header_name: env
        value_match:
          exact: prod
    on_match:
      action:
        typed_config:
          '@type': type.googleapis.com/xds.type.v3.Int32Range
          start: -100
"""

EXPECTED_FRAGMENT = """// .. validated-code-block:: yaml
//   :type-name: xds.type.matcher.v3.Matcher
//
//   matcher_list:
//     matchers:
//     - predicate:
//         single_predicate:
//           input:
//             typed_config:
//               '@type': type.googleapis.com/envoy.type.matcher.v3.HttpRequestHeaderMatchInput
//               header_name: env
//           value_match:
//             exact: staging
//       on_match:
//         action:
//           typed_config:
//             '@type': type.googleapis.com/envoy.type.v3.TokenBucket
//             max_tokens: 120
//     - predicate:
//         single_predicate:
//           input:
//             typed_config:
//               '@type': type.googleapis.com/envoy.type.matcher.v3.HttpRequestHeaderMatchInput
//               header_name: env
//           value_match:
//             exact: prod
//       on_match:
//         action:
//           typed_config:
//             '@type': type.googleapis.com/xds.type.v3.Int32Range
//             start: -100
//
"""


class TextprotoToYamlTest(unittest.TestCase):
    def setUp(self) -> None:
        testdata_path = pathlib.Path(pathlib.Path.cwd(), 'tools',
                                     'testdata',
                                     'config_validation')
        self.textproto_path = testdata_path / 'input.textproto'
        # Enable full diffs for multiline checks
        self.maxDiff = None

    def test_textproto_file_to_yaml(self):
        result_yaml = textproto2yaml.textproto_file_to_yaml(
            self.textproto_path, MESSAGE_TYPE)
        self.assertMultiLineEqual(result_yaml, EXPECTED_YAML)

    def test_yaml_to_fragment(self):
        result_yaml = textproto2yaml.textproto_file_to_yaml(
            self.textproto_path, MESSAGE_TYPE)
        result_fragment = textproto2yaml.yaml_to_fragment(result_yaml,
                                                          MESSAGE_TYPE)
        self.assertMultiLineEqual(result_fragment, EXPECTED_FRAGMENT)


if __name__ == '__main__':
    unittest.main()
