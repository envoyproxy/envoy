# Building Envoy with Bazel

## Installing Bazelisk as Bazel

It is recommended to use [Bazelisk](https://github.com/bazelbuild/bazelisk) installed as `bazel`, to avoid Bazel compatibility issues.

On Linux, run the following commands:

```console
sudo wget -O /usr/local/bin/bazel https://github.com/bazelbuild/bazelisk/releases/latest/download/bazelisk-linux-$([ $(uname -m) = "aarch64" ] && echo "arm64" || echo "amd64")
sudo chmod +x /usr/local/bin/bazel
```

On macOS, run the following command:
```console
brew install bazelisk
```

On Windows, run the following commands:
```cmd
mkdir %USERPROFILE%\bazel
powershell Invoke-WebRequest https://github.com/bazelbuild/bazelisk/releases/latest/download/bazelisk-windows-amd64.exe -OutFile %USERPROFILE%\bazel\bazel.exe
set PATH=%USERPROFILE%\bazel;%PATH%
```

## Production environments

To build Envoy with Bazel in a production environment, where the [Envoy
dependencies](https://www.envoyproxy.io/docs/envoy/latest/start/building#requirements) are typically
independently sourced, the following steps should be followed:

1. Configure, build and/or install the [Envoy dependencies](https://www.envoyproxy.io/docs/envoy/latest/start/building#requirements).
1. `bazel build -c opt envoy` from the repository root.

### Building from a release tarball

To build Envoy from a release tarball, you can download a release tarball from Assets section in each release in project [Releases page](https://github.com/envoyproxy/envoy/releases).
Given all required [Envoy dependencies](https://www.envoyproxy.io/docs/envoy/latest/start/building#requirements) are installed, the following steps should be followed:

1. Download and extract source code of a release tarball from the Releases page. For example: https://github.com/envoyproxy/envoy/releases/tag/v1.24.0.
1. `python3 tools/github/write_current_source_version.py` from the repository root.
1. `bazel build -c opt envoy` from the repository root.

> **Note**: If the the `write_current_source_version.py` script is missing from the extracted source code directory, you can download it from [here](https://raw.githubusercontent.com/envoyproxy/envoy/main/tools/github/write_current_source_version.py).
> This script is used to generate SOURCE_VERSION that is required by [`bazel/get_workspace_status`](./get_workspace_status) to "stamp" the binary in a non-git directory.

> **Note**: To avoid rate-limiting by GitHub API, you can provide [a valid GitHub token](https://docs.github.com/en/authentication/keeping-your-account-and-data-secure/about-authentication-to-github#githubs-token-formats) to `GITHUB_TOKEN` environment variable.
> The environment variable name that holds the token can also be customized by setting `--github_api_token_env_name`.
> In a GitHub Actions workflow file, you can set this token from [`secrets.GITHUB_TOKEN`](https://docs.github.com/en/actions/security-guides/automatic-token-authentication#about-the-github_token-secret).

Examples:

```console
GITHUB_TOKEN=<GITHUB_TOKEN> python3 tools/github/write_current_source_version.py
MY_TOKEN=<GITHUB_TOKEN> python3 tools/github/write_current_source_version.py --github_api_token_env_name=MY_TOKEN
```

## Quick start Bazel build for developers

This section describes how to and what dependencies to install to get started building Envoy with Bazel.
If you would rather use a pre-build Docker image with required tools installed, skip to [this section](#building-envoy-with-the-ci-docker-image).

As a developer convenience, a [WORKSPACE](https://github.com/envoyproxy/envoy/blob/main/WORKSPACE) and
[rules for building a recent
version](https://github.com/envoyproxy/envoy/blob/main/bazel/repositories.bzl) of the various Envoy
dependencies are provided. These are provided as is, they are only suitable for development and
testing purposes. The specific versions of the Envoy dependencies used in this build may not be
up-to-date with the latest security patches. See
[this doc](https://github.com/envoyproxy/envoy/blob/main/bazel/EXTERNAL_DEPS.md#updating-an-external-dependency-version)
for how to update or override dependencies.

1. Install external dependencies.
    ### Ubuntu
    On Ubuntu, run the following:
    ```console
    sudo apt-get install \
       autoconf \
       curl \
       libtool \
       patch \
       python3-pip \
       unzip \
       virtualenv
    ```

    ### Fedora
    On Fedora (maybe also other red hat distros), run the following:
    ```console
    dnf install \
        aspell-en \
        libatomic \
        libstdc++ \
        libstdc++-static \
        libtool \
        lld \
        patch \
        python3-pip
    ```

    ### Linux
    On Linux, we recommend using the prebuilt Clang+LLVM package from [LLVM official site](http://releases.llvm.org/download.html).
    Extract the tar.xz and run the following:
    ```console
    bazel/setup_clang.sh <PATH_TO_EXTRACTED_CLANG_LLVM>
    ```

    This will setup a `clang.bazelrc` file in Envoy source root. If you want to make clang as default, run the following:
    ```console
    echo "build --config=clang" >> user.bazelrc
    ```

    Note: Either `libc++` or `libstdc++-7-dev` (or higher) must be installed.

    #### Config Flag Choices
    Different [config](https://docs.bazel.build/versions/master/guide.html#--config) flags specify the compiler libraries:

    - `--config=libc++` means using `clang` + `libc++`
    - `--config=clang` means using `clang` + `libstdc++`
    - no config flag means using `gcc` + `libstdc++`


    ### macOS
    On macOS, you'll need to install several dependencies. This can be accomplished via [Homebrew](https://brew.sh/):
    ```console
    brew install coreutils wget libtool go bazelisk clang-format autoconf aspell
    ```
    _notes_: `coreutils` is used for `realpath`, `gmd5sum` and `gsha256sum`

    _notes_: See Homebrew python setup notes: https://docs.brew.sh/Homebrew-and-Python.

    The full version of Xcode (not just Command Line Tools) is also required to build Envoy on macOS.
    Envoy compiles and passes tests with the version of clang installed by Xcode 11.1:
    Apple clang version 11.0.0 (clang-1100.0.33.8).

    #### Troubleshooting
    If you see some error messages like the following:
    ```console
    xcrun: error: SDK "macosx12.1" cannot be located
    xcrun: error: SDK "macosx12.1" cannot be located
    xcrun: error: unable to lookup item 'Path' in SDK 'macosx12.1'
    ```
    please check the installed sdk version.
    ```console
    xcrun --show-sdk-version
    ```

    If the sdk version is lower than the one in the error message, upgrade your Command Line Tools using the following commands:
    ```console
    sudo rm -rf /Library/Developer/CommandLineTools
    softwareupdate --all --install --force
    sudo xcode-select --install
    ```

    If the following error occurs during the compilation process:
    ```console
    xcode-select: error: tool 'xcodebuild' requires Xcode, but active developer directory '/Library/Developer/CommandLineTools' is a command line tools instance
    ```
    please execute the following command and retry:
    ```console
    sudo xcode-select -s /Applications/Xcode.app/Contents/Developer
    ```

    Having the binutils keg installed in Brew is known to cause issues due to putting an incompatible
    version of `ar` on the PATH, so if you run into issues building third party code like luajit
    consider uninstalling binutils.

    ### Windows

    > Note: These instructions apply to **Windows 10 SDK, version 1803 (10.0.17134.12)**. Earlier versions will not compile because the `afunix.h` header is not available. **The recommended Windows version is equal or later than Windows 10 SDK, version 1903 (10.0.18362.1)**

    Install bazelisk in the PATH using the `bazel.exe` executable name as described above in the first section.

    When building Envoy, Bazel creates very long path names. One way to work around these excessive path
    lengths is to change the output base directory for bazel to a very short root path. An example Bazel configuration
    to help with this is to use `C:\_eb` as the bazel base path. This and other preferences should be set up by placing
    the following bazelrc configuration line in a system `%ProgramData%\bazel.bazelrc` file or the individual
    user's `%USERPROFILE%\.bazelrc` file (rather than including it on every bazel command line):

    ```
    startup --output_base=C:/_eb
    ```

    Another option to shorten the output root for Bazel is to set the `USERNAME` environment variable in your shell
    session to a short value. Bazel uses this value when constructing its output root path if no explicit `--output_base`
    is set.

    Bazel also creates file symlinks when building Envoy. It's strongly recommended to enable file symlink support
    using [Bazel's instructions](https://docs.bazel.build/versions/master/windows.html#enable-symlink-support).
    For other common issues, see the
    [Using Bazel on Windows](https://docs.bazel.build/versions/master/windows.html) page.

    > The paths in this document are given as
    examples, make sure to verify you are using the correct paths for your environment. Also note
    that these examples assume using a `cmd.exe` shell to set environment variables etc., be sure
    to do the equivalent if using a different shell.

    [python3](https://www.python.org/downloads/): Specifically, the Windows-native flavor distributed
    by python.org. The POSIX flavor available via MSYS2, the Windows Store flavor and other distributions
    will not work. Add a symlink for `python3.exe` pointing to the installed `python.exe` for Envoy scripts
    and Bazel rules which follow POSIX python conventions. Add `pip.exe` to the PATH and install the `wheel`
    package.
    ```cmd
    mklink %USERPROFILE%\Python39\python3.exe %USERPROFILE%\Python39\python.exe
    set PATH=%USERPROFILE%\Python39;%PATH%
    set PATH=%USERPROFILE%\Python39\Scripts;%PATH%
    pip install wheel
    ```

    [Build Tools for Visual Studio 2019](https://visualstudio.microsoft.com/downloads/#build-tools-for-visual-studio-2019):
    For building with MSVC, you must install at least the VC++ workload.
    You may alternately install the entire Visual Studio 2019 and use the Build Tools installed in that
    package. Earlier versions of VC++ Build Tools/Visual Studio are not recommended or supported.
    If installed in a non-standard filesystem location, be sure to set the `BAZEL_VC` environment variable
    to the path of the VC++ package to allow Bazel to find your installation of VC++. NOTE: ensure that
    the `link.exe` that resolves on your PATH is from VC++ Build Tools and not `/usr/bin/link.exe` from MSYS2,
    which is determined by their relative ordering in your PATH.
    ```cmd
    set BAZEL_VC=%USERPROFILE%\VSBT2019\VC
    set PATH=%USERPROFILE%\VSBT2019\VC\Tools\MSVC\14.26.28801\bin\Hostx64\x64;%PATH%
    ```

    The Windows SDK contains header files and libraries you need when building Windows applications. Bazel always uses the latest, but you can specify a different version by setting the environment variable `BAZEL_WINSDK_FULL_VERSION`. See [bazel/windows](https://docs.bazel.build/versions/master/windows.html)

    [MSYS2 shell](https://msys2.github.io/): Install to a path with no spaces, e.g. C:\msys64.

    Set the `BAZEL_SH` environment variable to the path of the installed MSYS2 `bash.exe`
    executable. Additionally, setting the `MSYS2_ARG_CONV_EXCL` environment variable to a value
    of `*` is often advisable to ensure argument parsing in the MSYS2 shell behaves as expected.
    ```cmd
    set PATH=%USERPROFILE%\msys64\usr\bin;%PATH%
    set BAZEL_SH=%USERPROFILE%\msys64\usr\bin\bash.exe
    set MSYS2_ARG_CONV_EXCL=*
    set MSYS2_PATH_TYPE=inherit
    ```

    Set the `TMPDIR` environment variable to a path usable as a temporary directory (e.g.
    `C:\Windows\TEMP`), and create a directory symlink `C:\c` to `C:\`, so that the MSYS2
    path `/c/Windows/TEMP` is equivalent to the Windows path `C:/Windows/TEMP`:
    ```cmd
    set TMPDIR=C:/Windows/TEMP
    mklink /d C:\c C:\
    ```

    The TMPDIR path and MSYS2 `mktemp` command are used frequently by the `rules_foreign_cc`
    component of Bazel as well as Envoy's test scripts, causing problems if not set to a path
    accessible to both Windows and msys commands. [Note the `ci/windows_ci_steps.sh` script
    which builds envoy and run tests in CI creates this symlink automatically.]

    In the MSYS2 shell, install additional packages via pacman:
    ```
    pacman -S diffutils patch unzip zip
    ```

    [Git](https://git-scm.com/downloads): This version from the Git project, or the version
    distributed using pacman under MSYS2 will both work, ensure one is on the PATH:.
    ```cmd
    set PATH=%USERPROFILE%\Git\bin;%PATH%
    ```

    Lastly, persist environment variable changes.
    ``` cmd
    setx PATH "%PATH%"
    setx BAZEL_SH "%BAZEL_SH%"
    setx MSYS2_ARG_CONV_EXCL "%MSYS2_ARG_CONV_EXCL%"
    setx BAZEL_VC "%BAZEL_VC%"
    setx TMPDIR "%TMPDIR%"
    setx MSYS2_PATH_TYPE "%MSYS2_PATH_TYPE%"
    ```
    > On Windows the supported/recommended shell to interact with bazel is MSYS2. This means that all the bazel commands (i.e. build, test) should be executed from MSYS2.

1. Install Golang on your machine. This is required as part of building [BoringSSL](https://boringssl.googlesource.com/boringssl/+/HEAD/BUILDING.md)
   and also for [Buildifer](https://github.com/bazelbuild/buildtools) which is used for formatting bazel BUILD files.
   Make sure you have go version 1.17 or later.
1. `go install github.com/bazelbuild/buildtools/buildifier@latest` to install buildifier. You may need to set `BUILDIFIER_BIN` to `$GOPATH/bin/buildifier`
   in your shell for buildifier to work. If GOPATH is not set, it is $HOME/go by default.
1. `go install github.com/bazelbuild/buildtools/buildozer@latest` to install buildozer. You may need to set `BUILDOZER_BIN` to `$GOPATH/bin/buildozer`
   in your shell for buildozer to work. If GOPATH is not set, it is $HOME/go by default.
1. `bazel build envoy` from the Envoy source directory. Add `-c opt` for an optimized release build or
   `-c dbg` for an unoptimized, fully instrumented debugging build.

## Building Envoy with the CI Docker image

Envoy can also be built with the Docker image used for CI, by installing Docker and executing the following.

On Linux, run:

```
./ci/run_envoy_docker.sh './ci/do_ci.sh dev'
```

From a Windows host with Docker installed, the Windows containers feature enabled, and bash (installed via
MSYS2 or Git bash), run:

**Note: the command below executes the whole Windows CI and unlike Linux you are not able to set specific build targets. You can modify `./ci/windows_ci_steps.sh` to modify `bazel` arguments, tests to run, etc. as well as set environment variables to adjust your container build environment.**

```
./ci/run_envoy_docker.sh './ci/windows_ci_steps.sh'
```

See also the [documentation](https://github.com/envoyproxy/envoy/tree/main/ci) for developer use of the
CI Docker image.

## Building Envoy with Remote Execution

Envoy can also be built with Bazel [Remote Execution](https://docs.bazel.build/versions/master/remote-execution.html),
part of the CI is running with the hosted [GCP RBE](https://blog.bazel.build/2018/10/05/remote-build-execution.html) service.

To build Envoy with a remote build services, run Bazel with your remote build service flags and with `--config=remote-clang`.
For example the following command runs build with the GCP RBE service used in CI:

```
bazel build envoy --config=remote-clang \
    --remote_cache=grpcs://remotebuildexecution.googleapis.com \
    --remote_executor=grpcs://remotebuildexecution.googleapis.com \
    --remote_instance_name=projects/envoy-ci/instances/default_instance
```

Change the value of `--remote_cache`, `--remote_executor` and `--remote_instance_name` for your remote build services. Tests can
be run in remote execution too.

Note: Currently the test run configuration in `.bazelrc` doesn't download test binaries and test logs,
to override the behavior set [`--remote_download_outputs`](https://docs.bazel.build/versions/master/command-line-reference.html#flag--remote_download_outputs)
accordingly.

## Building Envoy with Docker sandbox

Building Envoy with Docker sandbox uses the same Docker image used in CI with fixed C++ toolchain configuration. It produces more consistent
output which is not depending on your local C++ toolchain. It can also help debugging issues with RBE. To build Envoy with Docker sandbox:

```
bazel build envoy --config=docker-clang
```

Tests can be run in docker sandbox too. Note that the network environment, such as IPv6, may be different in the docker sandbox so you may want
set different options. See below to configure test IP versions.

## Linking against libc++ on Linux

To link Envoy against libc++, follow the [quick start](#quick-start-bazel-build-for-developers) to setup Clang+LLVM and run:
```
bazel build --config=libc++ envoy
```

Or use our configuration with Remote Execution or Docker sandbox, pass `--config=remote-clang-libc++` or
`--config=docker-clang-libc++` respectively.

If you want to make libc++ as default, add a line `build --config=libc++` to the `user.bazelrc` file in Envoy source root.

## Using a compiler toolchain in a non-standard location

By setting the `CC` and `LD_LIBRARY_PATH` in the environment that Bazel executes from as
appropriate, an arbitrary compiler toolchain and standard library location can be specified. One
slight caveat is that (at the time of writing), Bazel expects the binutils in `$(dirname $CC)` to be
unprefixed, e.g. `as` instead of `x86_64-linux-gnu-as`.

Note: this configuration currently doesn't work with Remote Execution or Docker sandbox, you have to generate a
custom toolchains configuration for them. See [bazelbuild/bazel-toolchains](https://github.com/bazelbuild/bazel-toolchains)
for more details.

## Supported compiler versions

We now require Clang >= 9 due to C++17 support and tcmalloc requirement. GCC >= 9 is also known to work.
Currently the CI is running with Clang 14.

## Clang STL debug symbols

By default Clang drops some debug symbols that are required for pretty printing to work correctly.
More information can be found [here](https://bugs.llvm.org/show_bug.cgi?id=24202). The easy solution
is to set ```--copt=-fno-limit-debug-info``` on the CLI or in your .bazelrc file.

## Removing debug info

If you don't want your debug or release binaries to contain debug info
to reduce binary size, pass `--define=no_debug_info=1` when building.
This is primarily useful when building envoy as a static library. When
building a linked envoy binary you can build the implicit `.stripped`
target from [`cc_binary`](https://docs.bazel.build/versions/master/be/c-cpp.html#cc_binary)
or pass [`--strip=always`](https://docs.bazel.build/versions/master/command-line-reference.html#flag--strip)
instead.

# Running the built Envoy binary on the host system

After Envoy is built, it can be executed via CLI.

For example, if Envoy was built using the `bazel build -c opt //source/exe:envoy-static` command, then it can be executed from the project's root directory by running:

```console
$(bazel info bazel-genfiles)/source/exe/envoy-static --config-path /path/to/your/envoy/config.yaml
```

# Testing Envoy with Bazel

All the Envoy tests can be built and run with:

```
bazel test //test/...
```

An individual test target can be run with a more specific Bazel
[label](https://bazel.build/versions/master/docs/build-ref.html#Labels), e.g. to build and run only
the units tests in
[test/common/http/async_client_impl_test.cc](https://github.com/envoyproxy/envoy/blob/main/test/common/http/async_client_impl_test.cc):

```
bazel test //test/common/http:async_client_impl_test
```

To observe more verbose test output:

```
bazel test --test_output=streamed //test/common/http:async_client_impl_test
```

It's also possible to pass into an Envoy test additional command-line args via `--test_arg`. For
example, for extremely verbose test debugging:

```
bazel test --test_output=streamed //test/common/http:async_client_impl_test --test_arg="-l trace"
```

By default, testing exercises both IPv4 and IPv6 address connections. In IPv4 or IPv6 only
environments, set the environment variable ENVOY_IP_TEST_VERSIONS to "v4only" or
"v6only", respectively.

```
bazel test //test/... --test_env=ENVOY_IP_TEST_VERSIONS=v4only
bazel test //test/... --test_env=ENVOY_IP_TEST_VERSIONS=v6only
```

By default, tests are run with the [gperftools](https://github.com/gperftools/gperftools) heap
checker enabled in "normal" mode to detect leaks. For other mode options, see the gperftools
heap checker [documentation](https://gperftools.github.io/gperftools/heap_checker.html). To
disable the heap checker or change the mode, set the HEAPCHECK environment variable:

```
# Disables the heap checker
bazel test //test/... --test_env=HEAPCHECK=
# Changes the heap checker to "minimal" mode
bazel test //test/... --test_env=HEAPCHECK=minimal
```

If you see a leak detected, by default the reported offsets will require `addr2line` interpretation.
You can run under `--config=clang-asan` to have this automatically applied.

Bazel will by default cache successful test results. To force it to rerun tests:

```
bazel test //test/common/http:async_client_impl_test --cache_test_results=no
```

Bazel will by default run all tests inside a sandbox, which disallows access to the
local filesystem. If you need to break out of the sandbox (for example to run under a
local script or tool with [`--run_under`](https://docs.bazel.build/versions/master/user-manual.html#flag--run_under)),
you can run the test with `--strategy=TestRunner=local`, e.g.:

```
bazel test //test/common/http:async_client_impl_test --strategy=TestRunner=local --run_under=/some/path/foobar.sh
```
# Stack trace symbol resolution

Envoy can produce backtraces on demand and from assertions and other fatal
actions like segfaults. Where supported, stack traces will contain resolved
symbols, though not include line numbers. On systems where absl::Symbolization is
not supported, the stack traces written in the log or to stderr contain addresses rather
than resolved symbols. If the symbols were resolved, the address is also included at
the end of the line.

The `tools/stack_decode.py` script exists to process the output and do additional symbol
resolution including file names and line numbers. It requires the `addr2line` program be
installed and in your path. Any log lines not relevant to the backtrace capability are
passed through the script unchanged (it acts like a filter). File and line information
is appended to the stack trace lines.

The script runs in one of two modes. To process log input from stdin, pass `-s` as the first
argument, followed by the executable file path. You can postprocess a log or pipe the output
of an Envoy process. If you do not specify the `-s` argument it runs the arguments as a child
process. This enables you to run a test with backtrace post processing. Bazel sandboxing must
be disabled by specifying local execution. Example command line with
`run_under`:

```
bazel test -c dbg //test/server:backtrace_test
--run_under=//tools:stack_decode --strategy=TestRunner=local
--cache_test_results=no --test_output=all
```

Example using input on stdin:

```
bazel test -c dbg //test/server:backtrace_test --cache_test_results=no --test_output=streamed |& tools/stack_decode.py -s bazel-bin/test/server/backtrace_test
```

You will need to use either a `dbg` build type or the `opt` build type to get file and line
symbol information in the binaries.

By default main.cc will install signal handlers to print backtraces at the
location where a fatal signal occurred. The signal handler will re-raise the
fatal signal with the default handler so a core file will still be dumped after
the stack trace is logged. To inhibit this behavior use
`--define=signal_trace=disabled` on the Bazel command line. No signal handlers will
be installed.

# Running a single Bazel test under GDB

```
bazel build -c dbg //test/common/http:async_client_impl_test
bazel build -c dbg //test/common/http:async_client_impl_test.dwp
gdb bazel-bin/test/common/http/async_client_impl_test
```

We need to use `-c dbg` Bazel option to generate debugging symbols and without
that GDB will not be very useful. The debugging symbols are stored as separate
debugging information files (`.dwp` files) and we can build a DWARF package file
with `.dwp ` target. The `.dwp` file need to be presented in the same folder with the
binary for a full debugging experience.

# Running Bazel tests requiring privileges

Some tests may require privileges (e.g. CAP_NET_ADMIN) in order to execute. One option is to run
them with elevated privileges, e.g. `sudo test`. However, that may not always be possible,
particularly if the test needs to run in a CI pipeline. `tools/bazel-test-docker.sh` may be used in
such situations to run the tests in a privileged docker container.

The script works by wrapping the test execution in the current repository's circle ci build
container, then executing it either locally or on a remote docker container. In both cases, the
container runs with the `--privileged` flag, allowing it to execute operations which would otherwise
be restricted.

The command line format is:
`tools/bazel-test-docker.sh <bazel-test-target> [optional-flags-to-bazel]`

The script uses two optional environment variables to control its behaviour:

* `RUN_REMOTE=<yes|no>`: chooses whether to run on a remote docker server.
* `LOCAL_MOUNT=<yes|no>`: copy/mount local libraries onto the docker container.

Use `RUN_REMOTE=yes` when you don't want to run against your local docker instance. Note that you
will need to override a few environment variables to set up the remote docker. The list of variables
can be found in the [Documentation](https://docs.docker.com/engine/reference/commandline/cli/).

Use `LOCAL_MOUNT=yes` when you are not building with the Envoy build container. This will ensure
that the libraries against which the tests dynamically link will be available and of the correct
version.

## Examples

Running the http integration test in a privileged container:

```bash
tools/bazel-test-docker.sh  //test/integration:integration_test --jobs=4 -c dbg
```

Running the http integration test compiled locally against a privileged remote container:

```bash
setup_remote_docker_variables
RUN_REMOTE=yes MOUNT_LOCAL=yes tools/bazel-test-docker.sh  //test/integration:integration_test \
  --jobs=4 -c dbg
```

# Additional Envoy build and test options

In general, there are 3 [compilation
modes](https://docs.bazel.build/versions/master/user-manual.html#flag--compilation_mode)
that Bazel supports:

* `fastbuild`: `-O0`, aimed at developer speed (default).
* `opt`: `-O2 -DNDEBUG -ggdb3 -gsplit-dwarf`, for production builds and performance benchmarking.
* `dbg`: `-O0 -ggdb3 -gsplit-dwarf`, only debug symbols, no optimization.

You can use the `-c <compilation_mode>` flag to control this, e.g.

```
bazel build -c opt envoy
```

To override the compilation mode and optimize the build for binary size, you can
use the `sizeopt` configuration:

```
bazel build envoy --config=sizeopt
```

## Sanitizers

To build and run tests with the gcc compiler's [address sanitizer
(ASAN)](https://github.com/google/sanitizers/wiki/AddressSanitizer) and
[undefined behavior
(UBSAN)](https://developers.redhat.com/blog/2014/10/16/gcc-undefined-behavior-sanitizer-ubsan) sanitizer enabled:

```
bazel test -c dbg --config=asan //test/...
```

The ASAN failure stack traces include line numbers as a result of running ASAN with a `dbg` build above. If the
stack trace is not symbolized, try setting the ASAN_SYMBOLIZER_PATH environment variable to point to the
llvm-symbolizer binary (or make sure the llvm-symbolizer is in your $PATH).

If you have clang-5.0 or newer, additional checks are provided with:

```
bazel test -c dbg --config=clang-asan //test/...
```

[Thread sanitizer (TSAN)](https://github.com/google/sanitizers/wiki/ThreadSanitizerCppManual) tests rely on
a TSAN-instrumented version of libc++ and can be run under the docker sandbox:

```
bazel test -c dbg --config=docker-tsan //test/...
```

Alternatively, you can build a local copy of TSAN-instrumented libc++. Follow the [quick start](#quick-start-bazel-build-for-developers) instruction to setup Clang+LLVM environment. Download LLVM sources from the [LLVM official site](https://github.com/llvm/llvm-project)

```
curl -sSfL "https://github.com/llvm/llvm-project/archive/llvmorg-11.0.1.tar.gz" | tar zx

```

Configure and build a TSAN-instrumented libc++. Please note that `LLVM_USE_SANITIZER=Thread` preprocessor definition is used to enable TSAN instrumentation, and `CMAKE_INSTALL_PREFIX="/opt/libcxx_tsan"` defines the installation directory path.

```
mkdir tsan
pushd tsan

cmake -GNinja -DLLVM_ENABLE_PROJECTS="libcxxabi;libcxx" -DLLVM_USE_LINKER=lld -DLLVM_USE_SANITIZER=Thread -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_INSTALL_PREFIX="/opt/libcxx_tsan" "../llvm-project-llvmorg-11.0.1/llvm"
ninja install-cxx install-cxxabi

rm -rf /opt/libcxx_tsan/include
```

Generate local_tsan.bazelrc containing bazel configuration for tsan tests:

```
bazel/setup_local_tsan.sh </path/to/instrumented/libc++/home>

```

To execute TSAN tests using the local instrumented libc++ library pass `--config=local-tsan` to bazel:

```
bazel test --config=local-tsan //test/...
```

For [memory sanitizer (MSAN)](https://github.com/google/sanitizers/wiki/MemorySanitizer) testing,
it has to be run under the docker sandbox which comes with MSAN instrumented libc++:

```
bazel test -c dbg --config=docker-msan //test/...
```

To run the sanitizers on OS X, prefix `macos-` to the config option, e.g.:

```
bazel test -c dbg --config=macos-asan //test/...
```

## Log Verbosity

Log verbosity is controlled at runtime in all builds.

To obtain `nghttp2` traces, you can set `ENVOY_NGHTTP2_TRACE` in the environment for enhanced
logging at `-l trace`. For example, in tests:

```
bazel test //test/integration:protocol_integration_test --test_output=streamed \
  --test_arg="-l trace" --test_env="ENVOY_NGHTTP2_TRACE="
```

Similarly, `QUICHE` verbose logs can be enabled by setting `ENVOY_QUICHE_VERBOSITY=n` in the
environment where `n` is the desired verbosity level (e.g.
`--test_env="ENVOY_QUICHE_VERBOSITY=2"`.

## Disabling optional features

The following optional features can be disabled on the Bazel build command-line:

* Hot restart with `--define hot_restart=disabled`
* Google C++ gRPC client with `--define google_grpc=disabled`
* Backtracing on signals with `--define signal_trace=disabled`
* Active stream state dump on signals with `--define signal_trace=disabled` or `--define disable_object_dump_on_signal_trace=disabled`
* tcmalloc with `--define tcmalloc=disabled`. Also you can choose Gperftools' implementation of
  tcmalloc with `--define tcmalloc=gperftools` which is the default for builds other than x86_64 and aarch64.
* deprecated features with `--define deprecated_features=disabled`
* http3/quic with `--//bazel:http3=False`
* autolinking libraries with `--define=library_autolink=disabled`
* admin HTML home page with `--define=admin_html=disabled`
* admin functionality with `--define=admin_functionality=disabled`
* static extension registration with `--define=static_extension_registration=disabled`
* spdlogging functionality with `--define=enable_logging=disabled`

## Enabling optional features

The following optional features can be enabled on the Bazel build command-line:

* Exported symbols during linking with `--define exported_symbols=enabled`.
  This config will exports all symbols and results in larger binary size. If partial symbols export
  is required and target platform is Linux, then `bazel/exported_symbols.txt` can be used to land it.
* Perf annotation with `--define perf_annotation=enabled` (see
  source/common/common/perf_annotation.h for details).
* BoringSSL can be built in a FIPS-compliant mode with `--define boringssl=fips`
  (see [FIPS 140-2](https://www.envoyproxy.io/docs/envoy/latest/intro/arch_overview/security/ssl#fips-140-2) for details).
* ASSERT() can be configured to log failures and increment a stat counter in a release build with
  `--define log_fast_debug_assert_in_release=enabled`. SLOW_ASSERT()s can be included with `--define log_debug_assert_in_release=enabled`. The default behavior is to compile all debug assertions out of
  release builds so that the condition is not evaluated. This option has no effect in debug builds.
* memory-debugging (scribbling over memory after allocation and before freeing) with
  `--define tcmalloc=debug`. Note this option cannot be used with FIPS-compliant mode BoringSSL and
  tcmalloc is built from the sources of Gperftools.
* Default [path normalization](https://github.com/envoyproxy/envoy/issues/6435) with
  `--define path_normalization_by_default=true`. Note this still could be disable by explicit xDS config.
* Manual stamping via VersionInfo with `--define manual_stamp=manual_stamp`.
  This is needed if the `version_info_lib` is compiled via a non-binary bazel rules, e.g `envoy_cc_library`.
  Otherwise, the linker will fail to resolve symbols that are included via the `linktamp` rule, which is only available to binary targets.
  This is being tracked as a feature in: https://github.com/envoyproxy/envoy/issues/6859.
* Process logging for Android applications can be enabled with `--define logger=android`.
* Excluding assertions for known issues with `--define disable_known_issue_asserts=true`.
  A KNOWN_ISSUE_ASSERT is an assertion that should pass (like all assertions), but sometimes fails for some as-yet unidentified or unresolved reason. Because it is known to potentially fail, it can be compiled out even when DEBUG is true, when this flag is set. This allows Envoy to be run in production with assertions generally enabled, without crashing for known issues. KNOWN_ISSUE_ASSERT should only be used for newly-discovered issues that represent benign violations of expectations.
* Envoy can be linked to [`zlib-ng`](https://github.com/zlib-ng/zlib-ng) instead of
  [`zlib`](https://zlib.net) with `--define zlib=ng`.

## Enabling and disabling extensions

Envoy uses a modular build which allows extensions to be removed if they are not needed or desired.
Extensions that can be removed are contained in
[extensions_build_config.bzl](../source/extensions/extensions_build_config.bzl). Contrib build
extensions are contained in [contrib_build_config.bzl](../contrib/contrib_build_config.bzl). Note
that contrib extensions are only included by default when building the contrib executable and in
the default contrib images pushed to Docker Hub.

The extensions disabled by default can be enabled by adding the following parameter to Bazel, for example to enable
`envoy.filters.http.kill_request` extension, add `--//source/extensions/filters/http/kill_request:enabled`.
The extensions enabled by default can be disabled by adding the following parameter to Bazel, for example to disable
`envoy.wasm.runtime.v8` extension, add `--//source/extensions/wasm_runtime/v8:enabled=false`.
Note not all extensions can be disabled.

To enable a specific WebAssembly (Wasm) engine, you'll need to pass `--define wasm=[wasm_engine]`, e.g. `--define wasm=wasmtime` to enable the [wasmtime](https://wasmtime.dev/) engine. Supported engines are:

* `v8` (the default included engine)
* `wamr`
* `wasmtime`
* `wavm`

If you're building from a custom build repository, the parameters need to prefixed with `@envoy`, for example
`--@envoy//source/extensions/filters/http/kill_request:enabled`.

You may persist those options in `user.bazelrc` in Envoy repo or your `.bazelrc`.

Contrib extensions can be enabled and disabled similarly to above when building the contrib
executable. For example:

`bazel build //contrib/exe:envoy-static --//contrib/squash/filters/http/source:enabled=false`

Will disable the squash extension when building the contrib executable.

## Customize extension build config

You can also use the following procedure to customize the extensions for your build:

* The Envoy build assumes that a Bazel repository named `@envoy_build_config` exists which
  contains the file `@envoy_build_config//:extensions_build_config.bzl`. In the default build,
  a synthetic repository is created containing [extensions_build_config.bzl](../source/extensions/extensions_build_config.bzl).
* Start by creating a new Bazel workspace somewhere in the filesystem that your build can access.
  This workspace should contain:
  * Empty WORKSPACE file.
  * Empty BUILD file.
  * A copy of [extensions_build_config.bzl](../source/extensions/extensions_build_config.bzl).
  * Comment out any extensions that you don't want to build in your file copy.

To have your local build use your overridden configuration repository there are two options:

1. Use the [`--override_repository`](https://docs.bazel.build/versions/master/command-line-reference.html)
   CLI option to override the `@envoy_build_config` repo.
2. Use the following snippet in your WORKSPACE before you load the Envoy repository. E.g.,

```
workspace(name = "envoy_filter_example")

local_repository(
    name = "envoy_build_config",
    # Relative paths are also supported.
    path = "/somewhere/on/filesystem/envoy_build_config",
)

local_repository(
    name = "envoy",
    # Relative paths are also supported.
    path = "/somewhere/on/filesystem/envoy",
)

...
```

When performing custom builds, it is acceptable to include contrib extensions as well. This can
be done by including the desired Bazel paths from [contrib_build_config.bzl](../contrib/contrib_build_config.bzl)
into the overridden `extensions_build_config.bzl`. (There is no need to specifically perform
a contrib build to include a contrib extension.)

## Extra extensions

If you are building your own Envoy extensions or custom Envoy builds and encounter visibility
problems with, you may need to adjust the default visibility rules to be public,
as documented in
[extensions_build_config.bzl](../source/extensions/extensions_build_config.bzl).
See the instructions above about how to create your own custom version of
[extensions_build_config.bzl](../source/extensions/extensions_build_config.bzl).

# Release builds

Release builds should be built in `opt` mode, processed with `strip` and have a
`.note.gnu.build-id` section with the Git SHA1 at which the build took place.
They should also ignore any local `.bazelrc` for reproducibility. This can be
achieved with:

```
bazel --bazelrc=/dev/null build -c opt envoy.stripped
```

One caveat to note is that the Git SHA1 is truncated to 16 bytes today as a
result of the workaround in place for
https://github.com/bazelbuild/bazel/issues/2805.

# Coverage builds

To generate coverage results, make sure you are using a Clang toolchain and have `llvm-cov` and
`llvm-profdata` in your `PATH`. Then run:

```
test/run_envoy_bazel_coverage.sh
```

**Note** that it is important to ensure that the versions of `clang`, `llvm-cov` and `llvm-profdata`
are consistent and that they match the most recent Clang/LLVM toolchain version in use by Envoy (see
the [build container
toolchain](https://github.com/envoyproxy/envoy-build-tools/blob/main/build_container/build_container_ubuntu.sh) for reference).

The summary results are printed to the standard output and the full coverage
report is available in `generated/coverage/coverage.html`.

To generate coverage results for fuzz targets, use the `FUZZ_COVERAGE` environment variable, e.g.:
```
FUZZ_COVERAGE=true VALIDATE_COVERAGE=false test/run_envoy_bazel_coverage.sh
```
This generates a coverage report for fuzz targets after running the target for one minute against fuzzing engine libfuzzer using its coprus as initial seed inputs. The full coverage report will be available in `generated/fuzz_coverage/coverage.html`.

Coverage for every PR is available in Circle in the "artifacts" tab of the coverage job. You will
need to navigate down and open "coverage.html" but then you can navigate per normal. NOTE: We
have seen some issues with seeing the artifacts tab. If you can't see it, log out of Circle, and
then log back in and it should start working.

The latest coverage report for main is available
[here](https://storage.googleapis.com/envoy-postsubmit/main/coverage/index.html). The latest fuzz coverage report for main is available [here](https://storage.googleapis.com/envoy-postsubmit/main/fuzz_coverage/index.html).

It's also possible to specialize the coverage build to a specified test or test dir. This is useful
when doing things like exploring the coverage of a fuzzer over its corpus. This can be done by
passing coverage targets as the command-line arguments and using the `VALIDATE_COVERAGE` environment
variable, e.g. for a fuzz test:

```
FUZZ_COVERAGE=true VALIDATE_COVERAGE=false test/run_envoy_bazel_coverage.sh //test/common/common:base64_fuzz_test
```

# Cleaning the build and test artifacts

`bazel clean` will nuke all the build/test artifacts from the Bazel cache for
Envoy proper. To remove the artifacts for the external dependencies run
`bazel clean --expunge`.

If something goes really wrong and none of the above work to resolve a stale build issue, you can
always remove your Bazel cache completely. It is likely located in `~/.cache/bazel`.

# Adding or maintaining Envoy build rules

See the [developer guide for writing Envoy Bazel rules](DEVELOPER.md).

# Bazel performance on (virtual) machines with low resources

If the (virtual) machine that is performing the build is low on memory or CPU
resources, you can override Bazel's default job parallelism determination with
`--jobs=N` to restrict the build to at most `N` simultaneous jobs, e.g.:

```
bazel build --jobs=2 envoy
```

# Debugging the Bazel build

When trying to understand what Bazel is doing, the `-s` and `--explain` options
are useful. To have Bazel provide verbose output on which commands it is executing:

```
bazel build -s envoy
```

To have Bazel emit to a text file the rationale for rebuilding a target:

```
bazel build --explain=file.txt envoy
```

To get more verbose explanations:

```
bazel build --explain=file.txt --verbose_explanations envoy
```

# Resolving paths in bazel build output

Sometimes it's useful to see real system paths in bazel error message output (vs. symbolic links).
`tools/path_fix.sh` is provided to help with this. See the comments in that file.

# Compilation database

Run `tools/gen_compilation_database.py` to generate
a [JSON Compilation Database](https://clang.llvm.org/docs/JSONCompilationDatabase.html). This could be used
with any tools (e.g. clang-tidy) compatible with the format. It is recommended to run this script
with `TEST_TMPDIR` set, so the Bazel artifacts doesn't get cleaned up in next `bazel build` or `bazel test`.

The compilation database could also be used to setup editors with cross reference, code completion.
For example, you can use [You Complete Me](https://valloric.github.io/YouCompleteMe/) or
[clangd](https://clangd.llvm.org/) with supported editors.

This requires Python 3.8.0+, download from [here](https://www.python.org/downloads/) if you do not have it installed already.

Use the following command to prepare a compilation database:

```
TEST_TMPDIR=/tmp tools/gen_compilation_database.py
```


# Running format linting without docker

Note that if you run the `check_spelling.py` script you will need to have `aspell` installed.

You can run clang-format directly, without docker:

```shell
bazel run //tools/code_format:check_format -- check
./tools/spelling/check_spelling_pedantic.py check
bazel run //tools/code_format:check_format -- fix
./tools/spelling/check_spelling_pedantic.py fix
```

# Advanced caching setup

Setting up an HTTP cache for Bazel output helps optimize Bazel performance and resource usage when
using multiple compilation modes or multiple trees.

## Setup local cache

You may use any [Remote Caching](https://docs.bazel.build/versions/master/remote-caching.html) backend
as an alternative to this.

This requires Go 1.11+, follow the [instructions](https://golang.org/doc/install#install) to install
if you don't have one. To start the cache, run the following from the root of the Envoy repository (or anywhere else
that the Go toolchain can find the necessary dependencies):

```
go run github.com/buchgr/bazel-remote --dir ${HOME}/bazel_cache --host 127.0.0.1 --port 28080 --max_size 64
```

See [Bazel remote cache](https://github.com/buchgr/bazel-remote) for more information on the parameters.
The command above will setup a maximum 64 GiB cache at `~/bazel_cache` on port 28080. You might
want to setup a larger cache if you run ASAN builds.

NOTE: Using docker to run remote cache server described in remote cache docs will likely have
slower cache performance on macOS due to slow disk performance on Docker for Mac.

Adding the following parameter to Bazel everytime or persist them in `.bazelrc`.

```
--remote_cache=http://127.0.0.1:28080/
```
