# Tooling docs

## Add a python tool

To demonstrate adding a python tool to the Envoy tooling lets go through the steps.

For this example a tool with the name `mytool.py` will be added to the `/tools/sometools` directory.

We will assume that `sometools` does not yet exist and will also need a `requirements.txt` file,
and `bazel` rule to configure the dependencies.

In most cases of adding a tool, it is likely you will not need to create a new set of dependencies, and
you can skip to the "Add python requirements" section.

We will also assume that you have `python3` and `pip` installed and working in your local environment.

The tool *must* be runnable with `bazel`, but we can also make it runnable directly without bazel, although the user
will then have to ensure they have the necessary dependencies locally installed themselves.

### Create the bazel boilerplate for a new set of python requirements

All python requirements for Envoy tooling  must be pinned with hashes to ensure the integrity of the dependencies.

Let's add the `bazel` boilerplate to setup a new `requirements.txt` file. This uses `rules_python`.

Open `bazel/repositories_extra.bzl` with your editor, and find the `_python_deps` function.

To this function, add the following bazel target:

```starlark
    pip_install(
        name = "sometools_pip3",
        requirements = "@envoy//tools/sometools:requirements.txt",
        extra_pip_args = ["--require-hashes"],
    )
```

Lets add an empty `tools/sometools/requirements.txt`:

```console

$ mkdir tools/sometools
$ touch tools/sometools/requirements.txt

```

We can now use `sometools_pip3` in the `BUILD` file for our python tool, although we will need
some actual requirements for it to be useful.

In order to ensure that this `requirements.txt` stays up-to-date we will also need to add an entry
in `.github/dependabot.yml`.

This example requires the following entry:

```yaml
  - package-ecosystem: "pip"
    directory: "/tools/sometools"
    schedule:
      interval: "daily"
```


### Add python requirements

For the purpose of this example, `mytool.py` will have dependencies on the `requests` and `pyyaml`
libraries.

Check on pypi for the most recent versions.

At the time of writing these were `2.25.1` and `5.4.1` for `requests` and `pyyaml` respectively.

Add the pinned dependencies to the `tools/sometools/requirements.txt` file:

```console

$ echo pyyaml==5.4.1 >> tools/sometools/requirements.txt
$ echo requests==2.25.1 >> tools/sometools/requirements.txt

```

So far we have not set the hashes for the requirements.

The easiest way to add the necessary hashes and dependencies is to use `pip-compile` from `pip-tools`.

This will pin all dependencies *of these libraries* too.

Run the following to update the `requirements.txt`:

```console

$ pip install pip-tools
$ pip-compile --generate-hashes tools/sometools/requirements.txt

```

### Add the python tool

For the purpose of this example we will add a trivial tool that dumps information
about a python package as `yaml`

Create a file `tools/sometools/mytool.py` with the following content:


```python
#!/usr/bin/env python3

import json
import sys

import requests
import yaml


def main(*args) -> int:
    sys.stdout.write(
        yaml.dump(
            requests.get(f"https://pypi.python.org/pypi/{args[0]}/json").json()))
    return 0


if __name__ == "__main__":
    sys.exit(main(*sys.argv[1:]))
```

### Create the `BUILD` file for `sometools`

If you are adding a tool to an existing toolset you can skip this step.

Add the following content to the file `tools/sometools/BUILD`

```starlark
load("@rules_python//python:defs.bzl", "py_binary")
load("@sometools_pip3//:requirements.bzl", "requirement")

licenses(["notice"])  # Apache 2
```

Note the loading of `requirement` from `@sometools_pip3`. We will use this
in the next section.

### Create the bazel target for the tool

Add `mytool.py` as a `py_binary` to the `tools/sometools/BUILD` file.

This will make the `mytool.py` file runnable as a `bazel` target.

```starlark
py_binary(
    name = "mytool",
    srcs = ["mytool.py"],
    visibility = ["//visibility:public"],
    deps = [
        requirement("requests"),
        requirement("pyyaml"),
    ],
)
```

With this added users that have bazel installed can run the tool with the following command:

```console
$ bazel run //tools/sometools:mytool PACKAGENAME
```


### Make the tool runnable without bazel

If you want users to be able to run the tool directly without bazel, you will need
to make it executable:

```console
$ chmod +x tools/sometools/mytool.py
```

With this added users that have the necessary python dependencies locally installed
can run the tool with the following command:

```console
$ ./tools/sometools/mytool.py PACKAGENAME

```

### Add unit tests for the tool

Envoy tooling is tested with `pytest`.

In order to use it with `bazel` we will need to add some boilerplate code.

Firstly add the following target to the `tools/sometools/BUILD` file:

```starlark
py_binary(
    name = "pytest_mytool",
    srcs = [
        "pytest_mytool.py",
        "tests/test_mytool.py",
    ],
    deps = [
        "//tools/testing:python_pytest",
    ],
    visibility = ["//visibility:public"],
)
```

The name is important and should be `pytest_TOOLNAME` - in this case `pytest_mytool`.

This target references two additional files that we need to add, the `pytest` runner file,
and the test file itself.

For the test runner create a file `tools/sometools/pytest_mytool.py` with the following code:

```python
#
# Runs pytest against the target:
#
#   //tools/sometools:mytool
#
# Can be run as follows:
#
#   bazel run //tools/sometools:pytest_mytool
#

import sys

from tools.testing import python_pytest


def main(*args) -> int:
    return python_pytest.main(*args, "--cov", "tools.sometools")


if __name__ == "__main__":
    sys.exit(main(*sys.argv[1:]))
```

Note the `--cov` argument. This should be set to the package name - in
this case `tools.sometools`.

This tells `pytest` to collect coverage in this package.

Finally we need the actual test file. This should be located in the `tools/sometools/tests`
directory. Create it now.

```console
$ mkdir tools/sometools/tests
```

Now add the following content to the `tools/sometools/test_mytool.py` file:

```python
from unittest.mock import patch

from tools.sometools import mytool


def test_mytool_main():
    with patch("tools.sometools.mytool.requests.get") as m_get:
        with patch("tools.sometools.mytool.yaml.dump") as m_yaml:
            assert mytool.main("PACKAGENAME") == 0
    assert (
        list(m_get.call_args)
        == [('https://pypi.python.org/pypi/PACKAGENAME/json',), {}])
    assert (
        list(m_get.return_value.json.call_args)
        == [(), {}])
    assert (
        list(m_yaml.call_args)
        == [(m_get.return_value.json.return_value,), {}])
```

This example use the mock library to patch all of the method calls, and
then tests that they have been called with the expected values.

You can then run the test:

```console
$ bazel run //tools/sometools:pytest_mytool
INFO: Analyzed target //tools/sometools:pytest_mytool (0 packages loaded, 0 targets configured).
INFO: Found 1 target...
Target //tools/sometools:pytest_mytool up-to-date:
  bazel-bin/tools/sometools/pytest_mytool
INFO: Elapsed time: 0.247s, Critical Path: 0.07s
INFO: 1 process: 1 internal.
INFO: Build completed successfully, 1 total action
================================ test session starts ===========================
platform linux -- Python 3.8.1, pytest-6.2.3, py-1.10.0, pluggy-0.13.1 -- /usr/bin/python3
cachedir: .pytest_cache
rootdir: /root/.cache/bazel/_bazel_root/f704bab1b165ed1368cb88f9f49e7532/execroot/envoy/bazel-out/k8-fastbuild/bin/tools/sometools/pytest_mytool.runfiles/envoy, configfile: pytest.ini
plugins: cov-2.11.1
collected 1 item

tools/sometools/tests/test_mytool.py::test_mytool_main PASSED                                                                                                                             [100%]

----------- coverage: platform linux, python 3.8.1-final-0 -----------
Name                                                        Stmts   Miss  Cover
-------------------------------------------------------------------------------
/src/workspace/envoy/tools/sometools/mytool.py                  7      0   100%
/src/workspace/envoy/tools/sometools/pytest_mytool.py           4      4     0%
/src/workspace/envoy/tools/sometools/tests/test_mytool.py      11      0   100%
-------------------------------------------------------------------------------
TOTAL                                                          22      4    82%


================================ 1 passed in 0.46s ============================
```

### The `patches` pytest fixture

When writing unit tests its not uncommon to need to patch a lot of different code.

A `patches` fixture has been added to make this easier.

The above test can be rewritten to make use of it as follows:

```python
from unittest.mock import patch

from tools.sometools import mytool


def test_mytool_main(patches):
    patched = patches(
        "requests.get",
        "yaml.dump",
        "sys.stdout.write",
        prefix="tools.sometools.mytool")

    with patched as (m_get, m_yaml, m_stdout):
        assert mytool.main("PACKAGENAME") == 0
    assert (
        list(m_get.call_args)
        == [('https://pypi.python.org/pypi/PACKAGENAME/json',), {}])
    assert (
        list(m_get.return_value.json.call_args)
        == [(), {}])
    assert (
        list(m_yaml.call_args)
        == [(m_get.return_value.json.return_value,), {}])
    assert (
        list(m_stdout.call_args)
        == [(m_yaml.return_value,), {}])

```

### Add a test for the pytest runner target

You will also want to add a test to ensure that the `pytest_mytool` runner target
is covered and does not have any mistakes.

A fixture - `check_pytest_target` is available for this purpose.

Add the following to `tools/sometools/tests/test_mytool.py`:

```
def test_pytest_python_coverage(check_pytest_target):
    check_pytest_target("tools.sometools.pytest_mytool")
```

### Debugging your code and tests

You will most likely want to make use of source-level debugging when writing tests

Add a breakpoint anywhere in your code or tests as follows:

```python
breakpoint()
```

This will drop you into the python debugger (`pdb`) at the breakpoint.


### Using the `tools.base.runner.Runner` class

A base class for writing tools that need to parse command line arguments has been provided.

To make use of it in this example we will need to add the runner as a dependency to the `mytool` target.

Edit `tools/sometools/BUILD` and change the `mytool` target to the following:

```starlark

py_binary(
    name = "mytool",
    srcs = ["mytool.py"],
    visibility = ["//visibility:public"],
    deps = [
        "//tools/base:runner",
        requirement("requests"),
        requirement("pyyaml"),
    ],
)

```

With this dependency in place we could rewrite the tool as follows:

```python
#!/usr/bin/env python3

import json
import sys

import requests
import yaml

from tools.base.runner import Runner


class Mytool(Runner):

    def add_arguments(self, parser):
        parser.add_argument("package", help="Package to fetch info for")

    def run(self) -> int:
        sys.stdout.write(
            yaml.dump(
                requests.get(
                    f"https://pypi.python.org/pypi/{self.args.package}/json").json()))
        return 0


def main(*args) -> int:
    return Mytool(*args).run()


if __name__ == "__main__":
    sys.exit(main(*sys.argv[1:]))
```
