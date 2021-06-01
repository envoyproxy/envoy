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


def track_prs():
    git = github.Github()
    repo = git.get_repo('envoyproxy/envoy')

    # The list of PRs tagged with the waiting label
    waiting_prs = []
    # The list of PRs which are not waiting, but are well within review SLO
    recent_prs = []
    # A dict of maintainer : outstanding_pr_string to be sent to slack
    maintainers_and_prs = {}
    # A placeholder for unassigned PRs, to be sent to #maintainers eventually
    maintainers_and_prs['unassigned'] = ""
    # Out-SLO PRs to be sent to #envoy-maintainer-oncall
    stalled_prs = ""

    # Snag all PRs, including drafts
    for pr_info in repo.get_pulls("open"):

        pr_title = pr_info.title
        pr_url = pr_info.html_url

        # If the PR is waiting, collect it in that list and continue.
        waiting = False
        for label in pr_info.labels:
            if label.name == 'waiting':
                waiting_prs.append("PR %s' is tagged as waiting" % (pr_title))
                waiting = True
                continue
        if waiting:
            continue

        # If the PR hasn't been updated recently, don't nag.
        # We can revisit this time, and always warn.
        now = datetime.datetime.now()
        time_zone_shift = datetime.timedelta(hours=4)
        pr_age = pr_info.updated_at - time_zone_shift
        delta = now - pr_age
        delta_days = delta.days
        delta_hours = delta.seconds // 3600
        if delta < datetime.timedelta(hours=8):
            recent_prs.append("PR %s' was updated %s hours ago" % (pr_title, delta_hours))
            continue

        # If we get to this point, the review may be in SLO - nudge if it's in
        # SLO, nudge in bold if not.
        message = ""
        if delta < datetime.timedelta(hours=get_slo_hours()):
            message = "<%s|%s> has been waiting %s days %s hours\n" % (
                pr_url, pr_title, delta_days, delta_hours)
        else:
            message = "<%s|%s> has been waiting *%s days %s hours*\n" % (
                pr_url, pr_title, delta_days, delta_hours)

        # If the PR has been out-SLO for over a day, inform on-call
        if delta > datetime.timedelta(hours=get_slo_hours() + 36):
            message = "<%s|%s> has been waiting *%s days %s hours*\n" % (
                pr_url, pr_title, delta_days, delta_hours)
            stalled_prs = stalled_prs + message

        # Add a reminder to each maintainer-assigner on the PR.
        has_maintainer_assignee = False
        for assignee_info in pr_info.assignees:
            assignee = assignee_info.login
            if assignee not in MAINTAINERS:
                continue
            has_maintainer_assignee = True
            if assignee not in maintainers_and_prs.keys():
                maintainers_and_prs[
                    assignee] = "Hello, %s, here are your PR reminders for the day \n" % assignee
            maintainers_and_prs[assignee] = maintainers_and_prs[assignee] + message

        # If there was no maintainer, track it as unassigned.
        if not has_maintainer_assignee:
            # don't bother assigning maintainer WIPs.
            if pr_info.draft and pr_info.user in maintainers_and_prs.keys():
                continue
            maintainers_and_prs['unassigned'] = maintainers_and_prs['unassigned'] + message

    # Return the dict of {maintainers : PR notifications}, and stalled PRs
    # comment this line out for the local local print statements below
    return maintainers_and_prs, stalled_prs

    print("RECENT PRS")
    for line in recent_prs:
        print(line)

    print("WAITING PRS")
    for line in waiting_prs:
        print(line)

    for key in maintainers_and_prs.keys():
        print(key)
        print(maintainers_and_prs[key])
        print('\n\n\n')


def post_to_maintainers(client, maintainers_and_messages):
    # Post updates to individual maintainers
    for key in maintainers_and_messages:
        message = maintainers_and_messages[key]

        # Only send messages if we have the maintainer UID
        # if key not in ['alyssawilk']:  # Use this line for debugging.
        if key not in MAINTAINERS:
            # Right now we skip "unassigned" but eventually that should go to #maintainers
            print("Skipping key %s " % key)
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
                text=("*Unassigned PRs*\n\n%s" % unassigned_prs))
            # TODO(alyssawilk) once maintainers start /wait tagging, uncomment this
            #response = client.chat_postMessage(
            #    channel='#envoy-maintainer-oncall', text=("*Stalled PRs*\n\n%s" % out_slo_prs))
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
