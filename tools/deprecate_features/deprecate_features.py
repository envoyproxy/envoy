# A simple script to snag deprecated proto fields and add them to runtime_features.h

from __future__ import print_function
import re
import subprocess
import fileinput


# Sorts out the list of deprecated proto fields which should be disallowed and returns a tuple of
# email and code changes.
def deprecate_proto():
  grep_output = subprocess.check_output('grep -r "deprecated = true" api/*', shell=True)

  filenames_and_fields = set()

  # Compile the set of deprecated fields and the files they're in, deduping via set.
  deprecated_regex = re.compile(r'.*\/([^\/]*.proto):[^=]* ([^= ]+) =.*')
  for line in grep_output.splitlines():
    match = deprecated_regex.match(line)
    if match:
      filenames_and_fields.add(tuple([match.group(1), match.group(2)]))
    else:
      print('no match in ' + line + ' please address manually!')

  # Now discard any deprecated features already listed in runtime_features
  exiting_deprecated_regex = re.compile(r'.*"envoy.deprecated_features.(.*):(.*)",.*')
  with open('source/common/runtime/runtime_features.cc', 'r') as features:
    for line in features.readlines():
      match = exiting_deprecated_regex.match(line)
      if match:
        filenames_and_fields.discard(tuple([match.group(1), match.group(2)]))

  # Finally sort out the code to add to runtime_features.cc and a canned email for envoy-announce.
  code_snippets = []
  email_snippets = []
  for (filename, field) in filenames_and_fields:
    code_snippets.append('    "envoy.deprecated_features.' + filename + ':' + field + '",\n')
    email_snippets.append(field + ' from ' + filename + '\n')
  code = ''.join(code_snippets)
  email = ''
  if email_snippets:
    email = ('\nThe following deprecated configuration fields will be disallowed by default:\n' +
             ''.join(email_snippets))

  return email, code


# Sorts out the list of features which should be default enabled and returns a tuple of
# email and code changes.
def flip_runtime_features():
  grep_output = subprocess.check_output(
      'grep -r "envoy.reloadable_features\." source/*', shell=True)

  features_to_flip = set()

  # Compile the set of features to flip, deduping via set.
  deprecated_regex = re.compile(r'.*"(envoy.reloadable_features\.[^"]+)".*')
  for line in grep_output.splitlines():
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
deprecate_email, deprecate_code = deprecate_proto()

email = ('The Envoy maintainer team is cutting the next Envoy release.  In the new release ' +
         runtime_email + deprecate_email)

print('\n\nSuggested envoy-announce email: \n')
print(email)

if not raw_input('Apply relevant runtime changes? [yN] ').strip().lower() in ('y', 'yes'):
  exit(1)

for line in fileinput.FileInput('source/common/runtime/runtime_features.cc', inplace=1):
  if 'envoy.reloadable_features.test_feature_true' in line:
    line = line.replace(line, line + runtime_features_code)
  if 'envoy.deprecated_features.deprecated.proto:is_deprecated_fatal' in line:
    line = line.replace(line, line + deprecate_code)
  print(line, end='')

print('\nChanges applied.  Please send the email above to envoy-announce.\n')
