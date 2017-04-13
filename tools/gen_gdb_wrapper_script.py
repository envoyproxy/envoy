#!/usr/bin/env python

# Support script for tools/bazel-test-gdb. This is passed to bazel test --run_under, and instead of
# running the test program it generates a wrapper program to allow for invoking gdb with the program
# in the bazel test environment. This is a workaround for the fact that --run_under does not attach
# stdin.

import base64
import json
import os
import string
import sys

GDB_RUNNER_SCRIPT = string.Template("""#!/usr/bin/env python

import base64
import json
import os

env = json.loads(base64.b64decode('${b64env}'))
for k, v in env.iteritems():
  os.environ[k] = v

os.chdir('${working_dir}')
os.system('gdb ${test_path}')
""")

if __name__ == '__main__':
  generated_path = sys.argv[1]
  test_path = sys.argv[2]
  with open(generated_path, 'w') as f:
    f.write(
        GDB_RUNNER_SCRIPT.substitute(
            b64env=base64.b64encode(json.dumps(dict(os.environ))),
            working_dir=os.getcwd(),
            test_path=test_path))
  # To make bazel consider the test a failure we exit non-zero.
  print 'Test was not run, instead a gdb wrapper script was produced in %s' % generated_path
  sys.exit(1)
