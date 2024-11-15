# Support tools

A collection of CLI tools meant to support and automate various aspects of
developing Envoy, particularly those related to code review. For example,
automatic DCO signoff and pre-commit format checking.

## Usage

To get started, you need only navigate to the Envoy project root and run:

```bash
./support/bootstrap
```

This will set up the development support toolchain automatically. The toolchain
uses git hooks extensively, copying them from `support/hooks` to the `.git`
folder.

The commit hook checks can be skipped using the `--no-verify` flags, as
so:

```bash
git commit --no-verify
```

You can also do this by adding `NO_VERIFY` to `.env`, for example:

```console
$ echo NO_VERIFY=1 >> .env
```

Or settting it in your environment:

```console
$ export NO_VERIFY=1
```

## Functionality

Currently the development support toolchain exposes two main pieces of
functionality:

* Automatically appending DCO signoff to the end of a commit message if it
  doesn't exist yet. Correctly covers edge cases like `commit --amend` and
  `rebase`.
* Automatically running DCO and format checks on all files in the diff, before
  push.

[filter]: https://github.com/envoyproxy/envoy-filter-example

## Fixing Format Problems

If the pre-push format checks detect any problems, you can either fix the
affected files manually or run the provided formatting script.

To run the format fix script directly:

```console
bazel run //tools/code_format:check_format -- fix && bazel run //tools/code:check -- fix -s main -v warn
```

To run the format fix script under Docker:

```console
./ci/run_envoy_docker.sh './ci/do_ci.sh format'
```

To run clang-tidy under Docker, run the following (this creates a full
compilation db and takes a long time):

```console
./ci/run_envoy_docker.sh ci/do_ci.sh clang_tidy
```
