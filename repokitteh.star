load('text', 'match')

use("github.com/repokitteh/modules/assign.star")
use("github.com/repokitteh/modules/review.star")
use("github.com/repokitteh/modules/wait.star")


def _circle(id, op, token):
    url = 'https://circleci.com/api/v1.1/project/github/envoyproxy/envoy/%d/%s?circle-token=%s' % (
      id,
      op,
      token,
    )        
      
    return http(
      secret_url=url,
      method='POST',
    )


def _cancel(get_secret, command):  
  ns = [int(a) for a in command.args]
  
  for n in ns:
    _circle(n, 'cancel', get_secret('circle_token'))
  

def _list():
  state, statuses = github_get_statuses()

  lines = []
  
  for status in statuses:
    if not status['context'].startswith('ci/circleci'):
      continue
      
    m = match(text=status['target_url'], pattern='/([0-9]+)\?')
    if m and len(m) == 2:
      job_id = int(m[1])
    else:
      job_id = 0
      
    lines.append('%d %s %s' % (job_id, status['state'], status['context']))
    
  github_issue_create_comment('```\n%s\n```' % '\n'.join(lines))
    

def _kick(command, get_secret):
  force = command.name[-1] == '!'

  state, statuses = github_get_statuses()
    
  failed_jobs = []
  
  for status in statuses:
    if not status['context'].startswith('ci/circleci'):
      continue
    
    if not (force or status['state'] in ['error', 'failure']):
      continue
      
    m = match(text=status['target_url'], pattern='/([0-9]+)\?')
    if m and len(m) == 2:
      failed_jobs.append(int(m[1]))
     
  rs = {}
  
  for j in failed_jobs:
    rs[j] = _circle(j, 'retry', get_secret('circle_token'))['status']
  
  github_issue_create_comment('%s %v' % (state, rs))
  
  
command(names=['kick', 'kick!'], func=_kick)
command(names='list', func=_list)
command(names='cancel', func=_cancel)
