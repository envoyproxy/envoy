# Release Process

## Active development

Active development is happening on the `main` branch, and a new versions will
be released on major development milestones and/or following upstream Envoy
releases.

## Cutting a release

* Update the [release notes](docs/root/intro/version_history.rst).
  * Add a new `Pending Release` section with `Bugfixes:` and `Features:` section at the top of the file.
  * Update the old `Pending Release` section with the release version and release date.
  * Make sure all changes since the last release (``git log `git rev-list --tags --max-count=1`..HEAD``) are reflected in the current release.
  * If building the quarterly release, also note the Envoy tagged release (e.g. as done in the [0.4.5](https://github.com/envoyproxy/envoy-mobile/pull/2000/files#diff-02cdc1a64b58714360c0cbdf245da06616364b110af4acd72ca364a524021eedR10) release
* If building the quarterly release, ensure the Envoy checkout points to the latest [envoy tagged release](https://github.com/envoyproxy/envoy/tags)
* Bump the version in [EnvoyMobile.podspec](EnvoyMobile.podspec) and [VERSION](VERSION) files
* Create a PR with the above changes, get it approved and merged
* Optionally, wait for CI to pass on main
* Draft a new release
  [here](https://github.com/envoyproxy/envoy-mobile/releases). Copy in the release notes you just created and publish it.
* Wait for the [artifacts run](https://github.com/envoyproxy/envoy-mobile/actions/workflows/artifacts.yml)
  for the tag release to finish running. Click on the workflow run to find the
  generated artifacts (e.g.  [this](https://github.com/envoyproxy/envoy-mobile/actions/runs/1638634901)).
  Download `envoy_android_aar_sources`, `envoy_ios_cocoapods`, and `envoy_ios_framework`.
* Go back to your tagged release, edit the release, and upload the 3 files from
  the previous step.
