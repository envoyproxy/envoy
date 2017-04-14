We welcome contributions from the community. Here are some guidelines.

# Communication

* Before starting work on a major feature, please reach out to us via GitHub, Gitter,
  email, etc. We will make sure no one else is already working on it and discuss high
  level design to make sure everyone is on the same page.
* Small patches and bug fixes don't need prior communication.

# Coding style

* See [STYLE.md](STYLE.md)

# Breaking configuration change policy

* As of the 1.3.0 release, the Envoy configuration is locked and we will not make breaking changes
  between releases.
* We reserve the right to deprecate configuration, and at the beginning of the following release
  cycle remove the deprecated configuration.
* All deprecations/breaking changes will be clearly listed in the release notes.

# Release cadence

* Currently we are targeting approximately quarterly official releases. We may change this based
  on customer demand.
* In general, master is assumed to be release candidate quality at all times for documented
  features. For undocumented or clearly under development features, use caution or ask about
  current status when running master. Lyft runs master in production, typically deploying every
  few days.

# PR review policy

* Typically we try to turn around reviews within one business day.
* See [OWNERS.md](OWNERS.md) for ther current list of committers.
* If possible, a senior committer should review every PR.
* Anyone is welcome to review any PR that they want, whether they are a committer or not.
* Committers **must** verify that the PR author has signed the CLA. Currently we do not have a
  bot that verifies CLA compliance, and only Lyft employees can check the CLA tool to see if
  someone has signed. Non-Lyft committers need to ask a Lyft employee to check unless they are sure
  the author has signed.

# Submitting a PR

* Fork the repo and create your PR.
* Tests will automatically run for you.
* When all of the tests are passing, tag @lyft/network-team and we will review it and
  merge once our CLA has been signed (see below).
* Party time.

# CLA

* We require a CLA for code contributions, so before we can accept a pull request we need
  to have a signed CLA. Please visit our [CLA service](https://oss.lyft.com/cla) and follow
  the instructions to sign the CLA.
