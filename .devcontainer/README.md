# Envoy Dev Container (experimental)

This directory contains some experimental tools for Envoy Development in [VSCode Remote - Containers](https://code.visualstudio.com/docs/remote/containers).

## How to use

Open with VSCode with the Container extension installed. Follow the [official guide](https://code.visualstudio.com/docs/remote/containers) to open this
repository directly from GitHub or from checked-out source tree.

After opening, run the `Refresh Compilation Database` task to generate compilation database to navigate in source code.
This will run partial build of Envoy and may take a while depends on the machine performance.
This task is needed to run everytime after:
- Changing a BUILD file that add/remove files from a target, changes dependencies
- Changing API proto files

There are additional tools for VS Code located in [`tools/vscode`](../tools/vscode) directory.

## Advanced Usages

### Using Remote Build Execution

Write the following content to `devcontainer.env` and rebuild the container. The key will be persisted in the container's `~/.bazelrc`.

```
GCP_SERVICE_ACCOUNT_KEY=<base64 encoded service account key>
BAZEL_REMOTE_INSTANCE=<RBE Instance>
BAZEL_REMOTE_CACHE=grpcs://remotebuildexecution.googleapis.com
BAZEL_BUILD_EXTRA_OPTIONS=--config=remote-ci --config=remote --jobs=<Number of jobs>
```

By default the `--config=remote` implies [`--remote_download_toplevel`](https://docs.bazel.build/versions/master/command-line-reference.html#flag--remote_download_toplevel),
change this to `minimal` or `all` depending on where you're running the container by adding them to `BAZEL_BUILD_EXTRA_OPTIONS`.

### Disk performance

Docker for Mac/Windows is known to have disk performance issue, this makes formatting all files in the container very slow.
[Update the mount consistency to 'delegated'](https://code.visualstudio.com/docs/remote/containers-advanced#_update-the-mount-consistency-to-delegated-for-macos) is recommended.
