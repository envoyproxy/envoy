# Script for collecting PRs in need of review, and informing maintainers via
# slack

from __future__ import print_function

import datetime
from datetime import date
import os
import re
import sys

import github

try:
    input = raw_input  # Python 2
except NameError:
    pass  # Python 3


maintainers = {'alyssawilk': 'U78RP48V9',
               'mattklein1234': 'U5CALEVSL'
#maintainers = ['lizan', 'snowp', 'ggreenway', 'htuch', 'zuercher', 'phlax', 'jmarantz', 'antoniovicente', 'junr03', 'wrowe', 'alyssawilk', 'mattklein123', 'yanavlasov']
               }


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

    # Snag all PRs, including drafts
    for pr_info in repo.get_pulls("open"):

      pr_title = pr_info.title
      pr_url = pr_info.commits_url

      # If the PR is waiting, collect it in that list and continue.
      waiting = False
      for label in pr_info.labels:
        if label.name == 'waiting':
          waiting_prs.append("PR %s' is tagged as waiting" % (pr_title))
          waiting = True
          continue
      if (waiting):
        continue

      # If the PR hasn't been updated recently, don't nag.
      # We can revisit this time, and always warn.
      now = datetime.datetime.now()
      time_zone_shift = datetime.timedelta(hours = 4)
      pr_age = pr_info.updated_at - time_zone_shift
      delta = now - pr_age
      delta_days = delta.days
      delta_hours = delta.seconds // 3600
      if (delta < datetime.timedelta(hours = 8)):
        recent_prs.append("PR %s' was updated %s hours ago" % (pr_title, delta_hours))
        continue

      # If we get to this point, the review may be in SLO - nudge if it's in
      # SLO, nudge in bold if not.
      message = ""
      if (delta < datetime.timedelta(hours = 24)):
        message = "<%s|%s> has been waiting %s days %s hours\n" % (pr_url, pr_title, delta_days, delta_hours)
      else:
        message = "<%s|%s> has been waiting *%s days %s hours*\n" % (pr_url, pr_title, delta_days, delta_hours)

      # Add a reminder to each maintainer-assigner on the PR.
      has_maintainer_assignee = False;
      for assignee_info in pr_info.assignees:
          assignee = assignee_info.login
          if assignee not in maintainers:
              continue
          has_maintainer_assignee = True
          if assignee not in maintainers_and_prs.keys():
              maintainers_and_prs[assignee] = "Good morning, %s, here are your PR reminders for the day \n" % assignee
          maintainers_and_prs[assignee] = maintainers_and_prs[assignee] + message

      # If there was no maintainer, track it as unassigned.
      if not has_maintainer_assignee:
        maintainers_and_prs['unassigned'] = maintainers_and_prs['unassigned'] + message

    # Return the list of maintainers : PR notifications
    # comment this line out for the local local print statements below
    return maintainers_and_prs

    print("RECENT PRS")
    for line in recent_prs:
        print(line)

    print ("WAITING PRS")
    for line in waiting_prs:
        print (line)

    for key in maintainers_and_prs.keys():
        print (key)
        print (maintainers_and_prs[key])
        print('\n\n\n')


from slack_sdk import WebClient
from slack_sdk.errors import SlackApiError

def post_to_slack(SLACK_BOT_TOKEN, maintainers_and_messages):
  client = WebClient(token=SLACK_BOT_TOKEN)

  for key in maintainers_and_messages:
    message = maintainers_and_messages[key]

    # Only send messages if we have the maintainer UID
    if key not in maintainers:
      # Right now we skip "unassigned" but eventually that should go to #maintainers
      print ("Skipping key %s " % key)
      continue
    uid = maintainers[key]

    # Ship messages off to slack.
    try:
        print(maintainers_and_messages[key])
        response = client.conversations_open(users=uid, text="hello")
        channel_id = response["channel"]["id"]
        response = client.chat_postMessage(channel=channel_id, text=message)
    except SlackApiError as e:
      print("Unexpected error %s", e.response["error"])

if __name__ == '__main__':
  token = os.getenv('SLACK_BOT_TOKEN')
  if not token:
    print('Missing token: please export token from https://api.slack.com/apps/A023NPQQ33K/oauth?')
    sys.exit(1)

  maintainers_and_messages = track_prs()
  post_to_slack(token, maintainers_and_messages)
