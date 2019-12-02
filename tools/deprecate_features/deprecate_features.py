# A simple script to flip runtime flags true.

from __future__ import print_function
import re
import subprocess
import fileinput
from six.moves import input


# Sorts out the list of features which should be default enabled and returns a tuple of
# email and code changes.
def flip_runtime_features():
  grep_output = subprocess.check_output('grep -r "envoy.reloadable_features\." source/*',
                                        shell=True)

  features_to_flip = set()

  # Compile the set of features to flip, deduping via set.
  deprecated_regex = re.compile(r'.*"(envoy.reloadable_features\.[^"]+)".*')
  for byte_line in grep_output.splitlines():
    line = byte_line.decode('utf-8')
    match = deprecated_regex.match(line)
    if match:
      features_to_flip.add(match.group(1))
    else:
      print('no match in ' + line + ' please address manually!')

  # Exempt the two test flags.
  features_to_flip.remove('envoy.reloadable_features.my_feature_name')
  features_to_flip.remove('envoy.reloadable_features.test_feature_true')

  code_snippets = []
  email_snippets = []
  for (feature) in features_to_flip:
    code_snippets.append('    "' + feature + '",\n')
    email_snippets.append(feature + '\n')
  code = ''.join(code_snippets)
  email = ''
  if email_snippets:
    email = 'the following features will be defaulted to true:\n' + ''.join(email_snippets)

  return email, code


# Gather code and suggested email changes.
runtime_email, runtime_features_code = flip_runtime_features()

email = ('The Envoy maintainer team is cutting the next Envoy release.  In the new release ' +
         runtime_email)

print('\n\nSuggested envoy-announce email: \n')
print(email)

if not input('Apply relevant runtime changes? [yN] ').strip().lower() in ('y', 'yes'):
  exit(1)

for line in fileinput.FileInput('source/common/runtime/runtime_features.cc', inplace=1):
  if 'envoy.reloadable_features.test_feature_true' in line:
    line = line.replace(line, line + runtime_features_code)
  print(line, end='')

print('\nChanges applied.  Please send the email above to envoy-announce.\n')
