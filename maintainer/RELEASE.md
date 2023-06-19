# Project tools

Tools for opening, closing and syncing release and development branches.

## Commands

### `bazel run @envoy_repo//:release`

This command switches the repo to "release" mode by doing the following:

- remove `-dev` suffix from version in `VERSION.txt`
- set the date to today's UTC date in `changelogs/current.yaml`

By default running the `release` command will also run the [sync](#bazel-run-toolsprojectsync) action. This can
be disabled with `--nosync`.

All changes are committed on completion. This can be disabled with the `--nocommit` option.

This command can only be run when the repo is in "dev" mode.

*NB: Further changes should not be made to the branch when it is in "release" mode.*

#### Example: prepare branch for release

```console

# bazel run @envoy_repo//:release
...
ProjectRunner SUCCESS [version] 1.23.0
ProjectRunner SUCCESS [changelog] current: May 11, 2022
ProjectRunner SUCCESS [changelog] up to date
ProjectRunner SUCCESS [inventory] up to date
ProjectRunner INFO [git] add: VERSION.txt
ProjectRunner INFO [git] add: changelogs/current.yaml
ProjectRunner INFO [git] commit: "repo: Release `1.23.0`"
ProjectRunner NOTICE Release created (1.23.0): May 11, 2022

```

Which produces the following commit:

```console
# git show --compact-summary
commit 3bc20808c31ecde39c9b4b03d6bdeb52344ae667 (HEAD -> project-release)
Author: Your Name <you@example.com>
Date:   Wed May 11 13:11:51 2022 +0000

    repo: Release `1.23.0`

    Signed-off-by: Your Name <you@example.com>

 VERSION.txt             | 2 +-
 changelogs/current.yaml | 4 ++--
 2 files changed, 3 insertions(+), 3 deletions(-)

```

### `bazel run @envoy_repo//:dev`

This command switches the repo to "dev" mode by doing the following:

- increment and add `-dev` suffix to version in `VERSION.txt`
- move `changelogs/current.yaml` -> `changelogs/$VERSION.yaml`
- create new `changelogs/current.yaml` from template

By default running the `release` command will also run the [sync](#bazel-run-toolsprojectsync) action. This can
be disabled with `--nosync`.

All changes are committed on completion. This can be disabled with the `--nocommit` option.

This command can only be run when the repo is in `release` mode.

#### Example: open (`main`) branch to development

```console
# bazel run @envoy_repo//:dev
...
ProjectRunner SUCCESS [version] 1.24.0-dev
ProjectRunner SUCCESS [changelog] add: 1.23.0
ProjectRunner SUCCESS [changelog] up to date
ProjectRunner SUCCESS [inventory] up to date
ProjectRunner INFO [git] add: VERSION.txt
ProjectRunner INFO [git] add: changelogs/1.23.0.yaml
ProjectRunner INFO [git] add: changelogs/current.yaml
ProjectRunner INFO [git] commit: "repo: Dev `1.24.0-dev`"
ProjectRunner NOTICE Repo set to dev (1.24.0-dev)

```

Which produces the following commit:

```console
# git show --compact-summary
commit cfd462d8101558553bfbfc0421e56bafaf316e6f (HEAD -> project-release)
Author: Your Name <you@example.com>
Date:   Wed May 11 13:14:04 2022 +0000

    repo: Dev `1.24.0-dev`

    Signed-off-by: Your Name <you@example.com>

 VERSION.txt                  |   2 +-
 changelogs/1.23.0.yaml (new) | 116 ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
 changelogs/current.yaml      | 109 +++++--------------------------------------------------------------------------------------------------------
 3 files changed, 122 insertions(+), 105 deletions(-)

```

By default the minor version is incremented:

```console
# git diff HEAD~1 VERSION.txt
```
```diff
diff --git a/VERSION.txt b/VERSION.txt
index a6c2798a48..573ce34a59 100644
--- a/VERSION.txt
+++ b/VERSION.txt
@@ -1 +1 @@
-1.23.0
+1.24.0-dev

```

#### Example: open (`release/v1.23`) branch to development

For non-`main` release branches this command can be called with the `--patch` option. In this case only the patch version will be
incremented, rather than the minor version.

```console
# bazel run @envoy_repo//:dev -- --patch
...
ProjectRunner SUCCESS [version] 1.23.1-dev
ProjectRunner SUCCESS [changelog] add: 1.23.0
ProjectRunner SUCCESS [changelog] up to date
ProjectRunner SUCCESS [inventory] up to date
ProjectRunner INFO [git] add: VERSION.txt
ProjectRunner INFO [git] add: changelogs/1.23.0.yaml
ProjectRunner INFO [git] add: changelogs/current.yaml
ProjectRunner INFO [git] commit: "repo: Dev `1.23.1-dev`"
ProjectRunner NOTICE Repo set to dev (1.23.1-dev)

```

This changes the same files as without the `--patch` option:

```console
# git show --compact-summary
Author: Your Name <you@example.com>
Date:   Wed May 11 13:16:27 2022 +0000

    repo: Dev `1.23.1-dev`

    Signed-off-by: Your Name <you@example.com>

 VERSION.txt                  |   2 +-
 changelogs/1.23.0.yaml (new) | 116 ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
 changelogs/current.yaml      | 109 +++++--------------------------------------------------------------------------------------------------------
 3 files changed, 122 insertions(+), 105 deletions(-)

```

But, in this case, the *patch* version is incremented instead.

```console
# git diff HEAD~1 VERSION.txt
```
```diff
diff --git a/VERSION.txt b/VERSION.txt
index a6c2798a48..4d1e5d262c 100644
--- a/VERSION.txt
+++ b/VERSION.txt
@@ -1 +1 @@
-1.23.0
+1.23.1-dev

```

### `bazel run @envoy_repo//:sync`

This command synchronizes older release branches by doing the following:

- fetching any new changelogs for releases (parsing where required for older rst format)
- fetching any newly available `rst` object/link inventories (used to map version in the documentation)
- updating `docs/versions.yaml` with any new documentation mappings

By default this command is always run when running the [dev](#bazel-run-toolsprojectdev) or
[release](#bazel-run-toolsprojectrelease) commands.

All changes are committed on completion. This can be disabled with the `--nocommit` option.

This command can be useful for synchronizing historical releases to a development branch, as other minor versions
may follow their own release schedule, and documentation for earlier branches may not or may not be available during a release.

#### Example: sync changelog and inventory

In this example there is both a newer changelog available for the `v1.21.2` release, and
a newly available documentation inventory.

```console

# bazel run @envoy_repo//:sync
...
ProjectRunner SUCCESS [changelog] add: 1.21.2
ProjectRunner SUCCESS [inventory] update: 1.21 -> 1.21.2
ProjectRunner INFO [git] add: changelogs/1.21.2.yaml
ProjectRunner INFO [git] add: docs/inventories/v1.21/objects.inv
ProjectRunner INFO [git] add: docs/versions.yaml
ProjectRunner INFO [git] commit: "repo: Sync"
ProjectRunner NOTICE Repo synced
```

Version history docs will now be mapped for the `v1.21` release to the latest patch version - ie. `1.21.2`.

Likewise the documentation will now include `1.21.2` as part of its changelogs and version history.

The commit this creates:

```console
# git show
commit 05e6bb8b5d34758fb984044be512fb31f5557edc (HEAD -> project-release)
Author: Your Name <you@example.com>
Date:   Wed May 11 13:10:56 2022 +0000

    repo: Sync

    Signed-off-by: Your Name <you@example.com>

```
```diff
diff --git a/changelogs/1.21.2.yaml b/changelogs/1.21.2.yaml
new file mode 100644
index 0000000000..ddc79f2e88
--- /dev/null
+++ b/changelogs/1.21.2.yaml
@@ -0,0 +1,10 @@
+date: April 27, 2022
+
+minor_behavior_changes:
+- area: cryptomb
+  change: |
+    remove RSA PKCS1 v1.5 padding support.
+- area: perf
+  change: |
+    ssl contexts are now tracked without scan based garbage collection and greatly improved the performance on secret update.
+
diff --git a/docs/inventories/v1.21/objects.inv b/docs/inventories/v1.21/objects.inv
index c6579605b2..6f972f337f 100644
Binary files a/docs/inventories/v1.21/objects.inv and b/docs/inventories/v1.21/objects.inv differ
diff --git a/docs/versions.yaml b/docs/versions.yaml
index 21f15a5561..6e92af45d6 100644
--- a/docs/versions.yaml
+++ b/docs/versions.yaml
@@ -14,5 +14,5 @@
 "1.18": 1.18.4
 "1.19": 1.19.4
 "1.20": 1.20.3
-"1.21": 1.21.1
+"1.21": 1.21.2
 "1.22": 1.22.0

```

## Workflows

### Release new minor version (`main` release)

#### Close the branch

- fork `main` branch
- run `bazel run @envoy_repo//:release`
- create PR with committed changes
- wait for PR to land on `main` and for release to be tagged

At this point the `main` branch is in "release" mode and should not have any additional commits.

#### Reopen the branch

- fork `main` branch
- run `bazel run @envoy_repo//:dev`
- create PR with committed changes
- wait for PR to land on `main`
- continue with development

### Release new patch version (`vX.Y` release)

#### Close the branch

For example, using the `release/v1.23` branch:

- fork `release/v1.23` branch
- run `bazel run @envoy_repo//:release`
- create PR to the `release/v1.23` branch with committed changes
- wait for PR to land on `release/v1.23` and for release to be tagged

At this point the `release/v1.23` branch is in "release" mode and should not have any additional commits.

#### Reopen the branch

- fork `release/v1.23` branch
- run `bazel run @envoy_repo//:dev -- --patch`
- create PR with committed changes
- wait for PR to land on `release/v1.23`
- continue with development

### Sync a release branch

This can be done either on `main` or on another release branch, and would be typically done after an
_earlier_ release branch had made a release.

For example, supposing `release/v1.21` had just released `v1.21.7`, you might want to include
the changelog and the correct documentation mappings for this new version in all later release branches:

- `release/v1.22`
- `release/v1.23`
- ...
- `main`

As this step is done automatically when opening and closing a branch the most likely scenario for using
this is to bring new releases -> `main` to update the "latest" docs.

- fork `main` branch
- run `bazel run @envoy_repo//:sync`
- create a PR to the `main` branch with committed changes
