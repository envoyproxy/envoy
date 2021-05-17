# Tooling docs

## Add a Python tool

To demonstrate adding a Python tool to the Envoy tooling lets go through the steps.

For this example a tool with the name `mytool.py` will be added to the `/tools/sometools` directory.

We will assume that `sometools` does not yet exist and will also need a `requirements.txt` file,
and `bazel` rule to configure the dependencies.

In most cases of adding a tool, it is likely you will not need to create a new set of dependencies, and
you can skip to the ["Add Python requirements"](#add-python-requirements) section.

We will also assume that you have `python3` and `pip` installed and working in your local environment.

The tool *must* be runnable with `bazel`, but we can also make it runnable directly without bazel, although the user
will then have to ensure they have the necessary dependencies locally installed themselves.

### Create the bazel boilerplate for a new set of Python requirements

All Python requirements for Envoy tooling  must be pinned with hashes to ensure the integrity of the dependencies.

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

Let's add an empty `tools/sometools/requirements.txt`:

```console

$ mkdir tools/sometools
$ touch tools/sometools/requirements.txt

```

We can now use `sometools_pip3` in the `BUILD` file for our Python tool, although we will need
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


### Add Python requirements

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

### Add the Python tool

For the purpose of this example we will add a trivial tool that dumps information
about a Python package as `yaml`

Create a file `tools/sometools/mytool.py` with the following content:


```python
#!/usr/bin/env python3

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

If you are adding a tool to an existing toolset you may be able skip this step -
just make sure that `load` lines are present.

Add the following content to the file `tools/sometools/BUILD`

```starlark
load("//tools/base:envoy_python.bzl", "envoy_py_binary")
load("@sometools_pip3//:requirements.bzl", "requirement")

licenses(["notice"])  # Apache 2
```

Note the loading of `requirement` from `@sometools_pip3`, and the loading of `envoy_py_binary`.

We will use these in the next section.

### Create the bazel target for the tool

Add `mytool.py` as an `envoy_py_binary` to the `tools/sometools/BUILD` file.

This will make the `mytool.py` file runnable as a `bazel` target, and will
add a test runner to ensure the file is tested.

```starlark
envoy_py_binary(
    name = "tools.sometools.mytool",
    deps = [
        requirement("requests"),
        requirement("pyyaml"),
    ],
)
```

Note that the `envoy_py_binary` expects the full dotted name of the module - in this case,
`tools.sometool.mytool`.

This will create a runnable target with the name of just `mytool`.

With this added users that have `bazel` installed can run the tool with the following command:

```console
$ bazel run //tools/sometools:mytool PACKAGENAME
```


### Make the tool runnable without bazel

If you want users to be able to run the tool directly without `bazel`, you will need
to make it executable:

```console
$ chmod +x tools/sometools/mytool.py
```

With this, users that have the necessary Python dependencies locally installed
can run the tool directly with the following command:

```console
$ ./tools/sometools/mytool.py PACKAGENAME

```

### Add unit tests for the tool

Envoy tooling is tested with `pytest`.

The test runner expects a test file, in this case `tools/sometools/tests/test_mytool.py`.

First, create the required directory if it is not present.

```console
$ mkdir tools/sometools/tests
```

Now add the following content to the `tools/sometools/tests/test_mytool.py` file:

```python
from unittest.mock import patch

from tools.sometools import mytool


def test_mytool_main():
    with patch("tools.sometools.mytool.requests.get") as m_get:
        with patch("tools.sometools.mytool.yaml.dump") as m_yaml:
            with patch("tools.sometools.mytool.sys.stdout.write") as m_stdout:
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

This example use the mock library to patch all of the method calls, and
then tests that they have been called with the expected values.

You can run the test using the (automatically generated) `//tools/sometools:pytest_mytool` target as follows:

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
platform linux -- python 3.8.1, pytest-6.2.3, py-1.10.0, pluggy-0.13.1 -- /usr/bin/python3
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

### Debugging your code and tests

You will most likely want to make use of source-level debugging when writing tests.

Add a breakpoint anywhere in your code or tests as follows:

```python
breakpoint()
```

This will drop you into the Python debugger (`pdb`) at the breakpoint.

### Using the `tools.base.runner.Runner` class

A base class for writing tools that need to parse command line arguments has been provided.

To make use of it in this example we will need to add the runner as a dependency to the `tools.sometools.mytool` target.

Edit `tools/sometools/BUILD` and change the `tools.sometools.mytool` target to the following:

```starlark
envoy_py_binary(
    name = "tools.sometools.mytool",
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

This will add `help` to the tool and improve the end users experience of using it.

You can invoke the help menu using `bazel`:


```console

$ bazel run //tools/sometools:mytool -- -h
...
usage: mytool.py [-h] package

positional arguments:
  package     Package to fetch info for

optional arguments:
  -h, --help  show this help message and exit
```

or directly with `python`:

```console
$ ./tools/sometools/mytool.py -h
...
```

### Using the `tools.base.checker.Checker` class

A base class for writing checkers (for example, linting tools) has also been provided.

Any classes subclassing `tools.base.checker.Checker` should provide a tuple of `__class__.checks`.

For each named check in `checks` the `Checker` will expect a method of the same name with the prefix `check_`.

For example, setting `checks` to the tuple `("check1", "check2")` the `Checker` will run the methods `check_check1` and `check_check2` in order.

Let's look at an example.

First, we need to add the bazel target.

Edit `tools/sometools/BUILD` and add a `tools.sometools.mychecker` target with a dependency on the base `Checker`.

```starlark
envoy_py_binary(
    name = "tools.sometools.mychecker",
    deps = [
        "//tools/base:checker",
    ],
)
```

Next add the `MyChecker` class to `tools/sometools/mychecker.py` as follows:

```python
#!/usr/bin/env python3

import sys

from tools.base.checker import Checker


class MyChecker(Checker):
    checks = ("check1", "check2")

    def check_check1(self) -> None:
        # checking code for check1
        try:
            do_something()
        except NotSuchABadError:
            self.warn("check1", ["Doing something didn't work out quite as expected 8/"])
        except ATerribleError:
            self.error("check1", ["Oh noes, something went badly wrong! 8("])
        else:
            self.succeed("check1", ["All good 8)"])

    def check_check2(self) -> None:
        # checking code for check2
        try:
            do_something_else()
        except NotSuchABadError:
            self.warn("check2", ["Doing something else didn't work out quite as expected 8/"])
        except ATerribleError:
            self.error("check2", ["Oh noes, something else went badly wrong! 8("])
        else:
            self.succeed("check2", ["All good 8)"])


def main(*args) -> int:
    return MyChecker(*args).run()


if __name__ == "__main__":
    sys.exit(main(*sys.argv[1:]))
```

Just like with the `Runner` class described [above](#using-the-toolsbaserunnerrunner-class) you can
use both with and without `bazel`. To use without, you will need make it executable, and the end
user will need to have any dependencies locally installed.

Notice in the check methods the results of the check are logged to one of `self.error`, `self.warn`,
or `self.succeed`. Each takes a `list` of outcomes. The results will be summarized to the user at the
end of all checks.

Just like with `Runner` a help menu is automatically created, and you can add custom arguments if
required.

Also like `Runner`, any added `Checker` classes are expected to have unit tests, and a `pytest_mychecker` bazel target
is automatically added. With the above example, the test file should be located at `tools/sometools/tests/test_mychecker.py`.

One key difference with the `Checker` tools and its derivatives is that it expects a `path` either specified
with `--path` or as an argument. This is used as a context (for example the Envoy src directory) for
running the checks.
