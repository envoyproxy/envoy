# Sync envoyproxy organization users to envoyproxy/assignable team.
#
# This can be used for bulk cleanups if envoyproxy/assignable is not consistent
# with organization membership. In general, prefer to add new members by editing
# the envoyproxy/assignable in the GitHub UI, which will also cause an
# organization invite to be sent; this reduces the need to manually manage
# access tokens.
#
# Note: the access token supplied must have admin:org (write:org, read:org)
# permissions (and ideally be scoped no more widely than this). See Settings ->
# Developer settings -> Personal access tokens for access token generation.
# Ideally, these should be cleaned up after use.

import os
import sys

import github


def get_confirmation():
    """Obtain stdin confirmation to add users in GH."""
    return input('Add users to envoyproxy/assignable ? [yN] ').strip().lower() in ('y', 'yes')


def sync_assignable(access_token):
    organization = github.Github(access_token).get_organization('envoyproxy')
    team = organization.get_team_by_slug('assignable')
    organization_members = set(organization.get_members())
    assignable_members = set(team.get_members())
    missing = organization_members.difference(assignable_members)

    if not missing:
        print('envoyproxy/assignable is consistent with organization membership.')
        return 0

    print('The following organization members are missing from envoyproxy/assignable:')
    for m in missing:
        print(m.login)

    if not get_confirmation():
        return 1

    for m in missing:
        team.add_membership(m, 'member')


if __name__ == '__main__':
    access_token = os.getenv('GITHUB_TOKEN')
    if not access_token:
        print('Missing GITHUB_TOKEN')
        sys.exit(1)

    sys.exit(sync_assignable(access_token))
