# Envoy Extension Policy

## Quality requirements

All extensions contained in the main Envoy repository will be held to the same quality bar as the
core Envoy code. This includes coding style, code reviews, test coverage, etc. In the future we
may consider creating a sandbox repository for extensions that are not compiled/tested by default
and held to a lower quality standard, but that is out of scope currently.

## Adding new extensions

The following procedure will be used when proposing new extensions for inclusion in the repository:
  1. A GitHub issue should be opened describing the proposed extension as with any major feature
  proposal.
  2. All extensions must be sponsored by an existing maintainer. Sponsorship means that the
  maintainer will shepherd the extension through design/code reviews. Maintainers can self-sponsor
  extensions if they are going to write them, shepherd them, and maintain them.
  
     Sponsorship serves two purposes:
     * It ensures that the extension will ultimately meet the Envoy quality bar.
     * It makes sure that incentives are aligned and that extensions are not added to the repo without
     sufficient thought put into future maintenance.

     *If sponsorship cannot be found from an existing maintainer, an organization can consider
     [doing the work to become a maintainer](./GOVERNANCE.md#process-for-becoming-a-maintainer) in
     order to be able to self-sponsor extensions.*
  
  3. Each extension must have two reviewers proposed for reviewing PRs to the extension. Neither of
  the reviewers must be a senior maintainer. Existing maintainers (including the sponsor) and other
  contributors can count towards this number. The initial reviewers will be codified in the
  [CODEOWNERS](./CODEOWNERS) file for long term maintenance. These reviewers can be swapped out as
  needed.
  4. Any extension added via this process becomes a full part of the repository. This means that any
  API breaking changes in the core code will be automatically fixed as part of the normal PR process
  by other contributors.

## Removing existing extensions

As stated in the previous section, once an extension becomes part of the repository it will be
maintained by the collective set of Envoy contributors as needed.

However, if an extension has known issues that are not being rectified by the original sponsor and
reviewers or new contributors that are willing to step into the role of extension owner, a
[vote of the maintainers](./GOVERNANCE.md#conflict-resolution-and-voting) can be called to remove the
extension from the repository.

## Extension pull request reviews

Extension PRs must not modify core Envoy code. In the event that an extension requires changes to core
Envoy code, those changes should be submitted as a separate PR and will undergo the normal code review
process, as documented in the [contributor's guide](./CONTRIBUTING.md).

Extension PRs must be approved by at least one sponsoring maintainer and an extension reviewer. These
may be a single individual, but it is always preferred to have multiple reviewers when feasible.

In the event that the Extension PR author is a sponsoring maintainer and no other sponsoring maintainer
is available, another maintainer may be enlisted to perform a minimal review for style and common C++
anti-patterns. The Extension PR must still be approved by a non-maintainer reviewer.
