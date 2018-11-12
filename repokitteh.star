load('text', 'match')

use("github.com/repokitteh/modules/assign.star")
use("github.com/repokitteh/modules/review.star")
use("github.com/repokitteh/modules/wait.star")


def _circleci_retry(n):
  pass


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
      
    lines.append('%d %s %s' % (job_id, status['state'], status['description']))
    
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
  
  github_issue_create_comment('%s %s' % (state, ','.join([str(j) for j in failed_jobs])))
  
  
command(names=['kick', 'kick!'], func=_kick)
command(names='list', func=_list)
