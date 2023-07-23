.. _testing_custom_envoy:

Custom Envoy
============

For some changes, it's helpful to test with changes to the envoy module, either to add additional
information or verify that an upstream change will work well for Envoy Mobile.

For local test runs, you can simply ``cd envoy`` and make changes.  These changes will
not be reflected when you create a draft pull request so can not be used to verify
that the Envoy Mobile build bots will pass.

To test Envoy changes against CI, make those changes in your Envoy repo, push that branch to GitHub
and change the ``.gitmodules`` file in the Envoy Mobile repo to point to your Envoy branch like so::


  [submodule "envoy"]
    path = envoy
    url = https://github.com/[githubid]/envoy.git
    branch = [branch name]

You should then be able to test the changes locally with  ``git submodule update --init``
as well as remotely.
