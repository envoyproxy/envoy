.. _releasing_instructions:

Releasing
=========

The following workflow should be followed to create and publish a new Envoy Mobile
`release <https://github.com/envoyproxy/envoy-mobile/releases>`_.

Prepare for release
-------------------

Before cutting the release, submit and merge a PR with the following changes:

1. Bump the version in the :repo:`VERSION <VERSION>` file
2. Provide a brief overview of the release in the :repo:`version history documentation <docs/root/intro/version_history.rst>`

Publish release and artifacts
-----------------------------

After merging the above changes, a new release may be
`tagged <https://github.com/envoyproxy/envoy-mobile/releases>`_.

Upon tagging the release, jobs automatically kick off on main which:

- Creates a GitHub release, along with iOS & Android build artifacts
- Publishes the release to Maven & CocoaPods
- Publishes the most recent version of the Envoy Mobile docs


Pre-release versioning
======================

Pre-releases are published on a weekly basis. The versioning scheme we use is ``X.Y.Z.{yyyymmdd}``.
For example: January 25, 2020: ``0.3.1.20200125``.


GPG Key
======================

To update the GPG key, use the following steps:

1. Generate a new GPG public/private key pair and follow the interactive prompt.

    $ gpg --full-generate-key

2. As part of GPG key generation, you will create a PASSPHRASE. Note it down.
3. For the `Real Name`, enter `Envoy Release Bot` and for the `email`, enter `noreply@envoyproxy.io`.
4. After the generate key command has finished, run the following to see the key that was created:

    $ gpg --list-keys

5. Use the key ID from the `--list-keys` command to show the private key:

    $ gpg --armor --export-secret-keys $KEY_ID

6. Re-distribute the new public key:

    $ gpg --keyserver keyserver.ubuntu.com --send-keys $KEY_ID

7. Ask an Envoy GitHub repo admin to update the following secrets:

    .. code-block:: console

      EM_GPG_PASSPHRASE=<passphrase noted down from step 2>
      EM_GPG_KEY=<secret key from step 5>
