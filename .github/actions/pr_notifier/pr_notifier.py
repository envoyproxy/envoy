# Script for collecting PRs in need of review, and informing maintainers via
# slack

from __future__ import print_function

import datetime
import os
import sys

import github
from slack_sdk import WebClient
from slack_sdk.errors import SlackApiError

MAINTAINERS = {
    'alyssawilk': 'U78RP48V9',
    'mattklein123': 'U5CALEVSL',
    'lizan': 'U79E51EQ6',
    'snowp': 'U93KTPQP6',
    'ggreenway': 'U78MBV869',
    'htuch': 'U78E7055Z',
    'zuercher': 'U78J72Q82',
    'phlax': 'U017PLM0GNQ',
    'jmarantz': 'U80HPLBPG',
    'antoniovicente': 'UKVNCQ212',
    'junr03': 'U79K0Q431',
    'wrowe': 'UBQR8NGBS',
    'yanavlasov': 'UJHLR5KFS',
    'asraa': 'UKZKCFRTP',
}


def get_slo_hours():
    # on Monday, allow for 24h + 48h
    if datetime.date.today().weekday() == 0:
        return 72
    return 24


# Return true if the PR has a waiting tag, false otherwise.
def is_waiting(labels):
    for label in labels:
        if label.name == 'waiting' or label.name == 'waiting:any':
            return True
    return False


# Generate a pr message, bolding the time if it's out-SLO
def pr_message(pr_age, pr_url, pr_title, delta_days, delta_hours):
    if pr_age < datetime.timedelta(hours=get_slo_hours()):
        return "<%s|%s> has been waiting %s days %s hours\n" % (
            pr_url, pr_title, delta_days, delta_hours)
    else:
        return "<%s|%s> has been waiting *%s days %s hours*\n" % (
            pr_url, pr_title, delta_days, delta_hours)


# Adds reminder lines to the appropriate maintainer to review the assigned PRs
def add_reminders(assignees, maintainers_and_prs, message):
    has_maintainer_assignee = False
    for assignee_info in assignees:
        assignee = assignee_info.login
        if assignee not in MAINTAINERS:
            continue
        has_maintainer_assignee = True
        if assignee not in maintainers_and_prs.keys():
            maintainers_and_prs[
                assignee] = "Hello, %s, here are your PR reminders for the day \n" % assignee
        maintainers_and_prs[assignee] = maintainers_and_prs[assignee] + message
    return has_maintainer_assignee


def track_prs():
    git = github.Github()
    repo = git.get_repo('envoyproxy/envoy')

    # The list of PRs which are not waiting, but are well within review SLO
    recent_prs = []
    # A dict of maintainer : outstanding_pr_string to be sent to slack
    maintainers_and_prs = {}
    # A placeholder for unassigned PRs, to be sent to #maintainers eventually
    maintainers_and_prs['unassigned'] = ""
    # Out-SLO PRs to be sent to #envoy-maintainer-oncall
    stalled_prs = ""

    # Snag all PRs, including drafts
    for pr_info in repo.get_pulls("open", "updated", "desc"):
        # If the PR is waiting, continue.
        if is_waiting(pr_info.labels):
            continue

        # Update the time based on the time zone delta from github's
        pr_age = pr_info.updated_at - datetime.timedelta(hours=4)
        delta = datetime.datetime.now() - pr_age
        delta_days = delta.days
        delta_hours = delta.seconds // 3600

        # If we get to this point, the review may be in SLO - nudge if it's in
        # SLO, nudge in bold if not.
        message = pr_message(delta, pr_info.html_url, pr_info.title, delta_days, delta_hours)

        # If the PR has been out-SLO for over a day, inform on-call
        if delta > datetime.timedelta(hours=get_slo_hours() + 36):
            stalled_prs = stalled_prs + message

        # Add a reminder to each maintainer-assigner on the PR.
        has_maintainer_assignee = add_reminders(pr_info.assignees, maintainers_and_prs, message)

        # If there was no maintainer, track it as unassigned.
        if not has_maintainer_assignee:
            # don't bother assigning maintainer WIPs.
            if pr_info.draft and pr_info.user.login in maintainers_and_prs.keys():
                continue
            maintainers_and_prs['unassigned'] = maintainers_and_prs['unassigned'] + message

    # Return the dict of {maintainers : PR notifications}, and stalled PRs
    return maintainers_and_prs, stalled_prs


def post_to_maintainers(client, maintainers_and_messages):
    # Post updates to individual maintainers
    for key in maintainers_and_messages:
        message = maintainers_and_messages[key]

        # Only send messages if we have the maintainer UID
        if key not in MAINTAINERS:
            continue
        uid = MAINTAINERS[key]

        # Ship messages off to slack.
        try:
            print(maintainers_and_messages[key])
            response = client.conversations_open(users=uid, text="hello")
            channel_id = response["channel"]["id"]
            response = client.chat_postMessage(channel=channel_id, text=message)
        except SlackApiError as e:
            print("Unexpected error %s", e.response["error"])


def post_to_oncall(client, unassigned_prs, out_slo_prs):
    # Post updates to #envoy-maintainer-oncall
    unassigned_prs = maintainers_and_messages['unassigned']
    if unassigned_prs:
        try:
            response = client.chat_postMessage(
                channel='#envoy-maintainer-oncall',
                text=("*'Unassigned' PRs* (PRs with no maintainer assigned)\n%s" % unassigned_prs))
            response = client.chat_postMessage(
                channel='#envoy-maintainer-oncall', text=("*Stalled PRs*\n\n%s" % out_slo_prs))
        except SlackApiError as e:
            print("Unexpected error %s", e.response["error"])


if __name__ == '__main__':
    SLACK_BOT_TOKEN = os.getenv('SLACK_BOT_TOKEN')
    if not SLACK_BOT_TOKEN:
        print(
            'Missing SLACK_BOT_TOKEN: please export token from https://api.slack.com/apps/A023NPQQ33K/oauth?'
        )
        sys.exit(1)

    maintainers_and_messages, stalled_prs = track_prs()

    client = WebClient(token=SLACK_BOT_TOKEN)
    post_to_maintainers(client, maintainers_and_messages)
    post_to_oncall(client, maintainers_and_messages['unassigned'], stalled_prs)
