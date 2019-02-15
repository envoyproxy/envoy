# A simple script to snag deprecated proto fields and add them to runtime_features.h

import re
import subprocess
import fileinput

grep_output = subprocess.check_output('grep -r "deprecated = true" api/*', shell=True)

filenames_and_fields = set()

# Compile the set of deprecated fields and the files they're in, deduping via set.
deprecated_regex = re.compile(r'.*\/([^\/]*.proto):[^=]* ([^= ]+) =.*')
for line in grep_output.splitlines():
  match = deprecated_regex.match(line)
  if match:
    filenames_and_fields.add(tuple([match.group(1), match.group(2)]))
  else:
    print 'no match in ' + line + ' please address manually!'

# Now discard any deprecated features already listed in runtime_features
exiting_deprecated_regex = re.compile(r'.*"envoy.deprecated_features.(.*):(.*)",.*')
with open('source/common/runtime/runtime_features.h', 'r') as features:
  for line in features.readlines():
    match = exiting_deprecated_regex.match(line)
    if match:
      filenames_and_fields.discard(tuple([match.group(1), match.group(2)]))

# Finally sort out the code to add to runtime_features.h and a canned email for envoy-announce.
code = ''
email = 'The latest Envoy release will deprecate the following configuration fields:\n'
for (filename, field) in filenames_and_fields:
  code += ('    "envoy.deprecated_features.' + filename + ':' + field + '",\n')
  email += (field + ' from ' + filename + '\n')

print '\n\nSuggested runtime changes: '
print code

if not raw_input('Apply runtime changes? [yN] ').strip().lower() in ('y', 'yes'):
  exit(1)

for line in fileinput.FileInput('source/common/runtime/runtime_features.h', inplace=1):
  if 'envoy.deprecated_features.deprecated.proto:is_deprecated_fatal' in line:
    line = line.replace(line, line + code)
  print line,

print '\nChanges applied.  Please create an upstream PR and send the following to envoy-announce:\n'

print email
