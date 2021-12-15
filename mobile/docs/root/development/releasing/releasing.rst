.. _releasing_instructions:

Releasing
=========

The following workflow should be followed to create and publish a new Envoy Mobile
`release <https://github.com/envoyproxy/envoy-mobile/releases>`_.

Prepare for release
-------------------

Before cutting the release, submit and merge a PR with the following changes:

1. Bump the version in the :repo:`EnvoyMobile.podspec <EnvoyMobile.podspec>` file
2. Bump the version in the :repo:`VERSION <VERSION>` file
3. Provide a brief overview of the release in the :repo:`version history documentation <docs/root/intro/version_history.rst>`

Publish release and artifacts
-----------------------------

After merging the above changes, a new release may be
`tagged <https://github.com/envoyproxy/envoy-mobile/releases>`_.

When tagging a release, it should contain all the artifacts built by CI on the main commit being
tagged as the new version. These artifacts may be downloaded by clicking on the CI status of the
commit on main, then clicking ``Artifacts`` in the top right corner. After downloading, be sure
to upload the artifacts when tagging the GitHub release.

Upon tagging the release, jobs automatically kick off on main which:

- Publish an Android artifact to Maven
- Publish the most recent version of the Envoy Mobile docs

The last step to completing a release is to publish the CocoaPods artifact to the specs repo.
This can be done by any maintainer with write access to the spec definition. From the repository
directory, run:

``pod trunk push``

Note: This last step is slated to be automated in :issue:`#624 <624>`.


Pre-release versioning
======================

Pre-releases are published on a weekly basis. The versioning scheme we use is ``X.Y.Z.{mmddyy}``.
For example: January 25, 2020: ``0.3.1.012520``.
