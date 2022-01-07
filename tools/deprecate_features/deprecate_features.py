# A simple script to snag deprecated proto fields and add them to runtime_features.cc

from __future__ import print_function
import re
import subprocess
import fileinput

import envoy_repo


# Sorts out the list of deprecated proto fields which should be disallowed and returns a tuple of
# email and code changes.
def deprecate_proto():
    grep_output = subprocess.check_output(
        'grep -r "deprecated = true" api/*', shell=True, cwd=envoy_repo.PATH)

    filenames_and_fields = set()

    # Compile the set of deprecated fields and the files they're in, deduping via set.
    deprecated_regex = re.compile(r'.*\/([^\/]*.proto):[^=]* ([^= ]+) =.*')
    for byte_line in grep_output.splitlines():
        line = str(byte_line)
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
        email = (
            '\nThe following deprecated configuration fields will be disallowed by default:\n'
            + ''.join(email_snippets))

    return email, code


# Gather code and suggested email changes.
deprecate_email, deprecate_code = deprecate_proto()

email = (
    'The Envoy maintainer team is cutting the next Envoy release.  In the new release '
    + deprecate_email)

print('\n\nSuggested envoy-announce email: \n')
print(email)

if not input('Apply relevant runtime changes? [yN] ').strip().lower() in ('y', 'yes'):
    exit(1)

for line in fileinput.FileInput('source/common/runtime/runtime_features.cc', inplace=1):
    if 'envoy.deprecated_features.deprecated.proto:is_deprecated_fatal' in line:
        line = line.replace(line, line + deprecate_code)
    print(line, end='')

print('\nChanges applied.  Please send the email above to envoy-announce.\n')
