# Tooling docs

## Add a Python tool

To demonstrate adding a Python tool to the Envoy tooling lets go through the steps.

For this example a tool with the name `mytool.py` will be added to the `/tools/sometools` directory.

The tool *must* be runnable with `bazel`, but we can also make it runnable directly without bazel, although the user
will then have to ensure they have the necessary dependencies locally installed themselves.

### Add Python requirements

For the purpose of this example, `mytool.py` will have dependencies on the `foolib`
libraries.

First check to see if `foolib` is already specified in `requirements.in`.

```console
$ grep foolib tools/base/requirements.in
```

Assuming its not, you can add with:

```console
$ echo footool >> tools/base/requirements.in
```

Run the following to update the `requirements.txt`:

```console

$ ./ci/run_envoy_docker.sh bazel run //tools/base:requirements.update

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
load("@base_pip3//:requirements.bzl", "requirement")

licenses(["notice"])  # Apache 2
```

Note the loading of `requirement` from `@base_pip3`, and the loading of `envoy_py_binary`.

We will use these in the next section.

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
        requirement("aio.run.runner"),
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

from aio.run.runner import Runner


class Mytool(Runner):

    def add_arguments(self, parser):
        parser.add_argument("package", help="Package to fetch info for")

    async def run(self) -> int:

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
