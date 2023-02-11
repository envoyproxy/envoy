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

On 2024-04-20 the GPG key used to sign releases will expire. To extend the key's expiration date,
follow these steps:

Import the key locally::

    $ echo $GPG_KEY | base64 --decode > signing-key
    $ gpg --passphrase $GPG_PASSPHRASE --batch --import signing-key
    $ gpg --list-keys

Follow the instructions here on
`Dealing with Expired Keys <https://central.sonatype.org/publish/requirements/gpg/#dealing-with-expired-keys>`_
to extend the key and sub key expiration dates.

Re-distribute the new public key:

    $ gpg --keyserver keyserver.ubuntu.com --send-keys $KEY_ID

Export the public/private keys, store them in a safe place::

    $ gpg -a --export $KEY_ID > envoy.mobile.gpg.public
    $ gpg -a --export-secret-keys $KEY_ID > envoy.mobile.gpg.private

Update the GitHub Action ``GPG_KEY`` secret with the Base64 encoded value
of the private key.

    $ cat envoy.mobile.gpg.private | base64
