use("github.com/repokitteh/modules/assign.star")
use("github.com/repokitteh/modules/review.star")
use("github.com/repokitteh/modules/wait.star")

load('text', 'match')

def _kick(command, get_secret):
  state, statuses = github_get_statuses()
    
  failed_jobs = []
  
  for status in statuses:
    if not status['context'].startswith('ci/circleci'):
      continue
    
    if not (status['state'] in ['error', 'failure']):
      continue
      
    m = match(text=status['target_url'], pattern='/([0-9]+)\?')
    if m and len(m) == 2:
      failed_jobs.append(m[1])
  
  github_issue_create_comment('%s %s' % (state, ','.join([str(j) for j in failed_jobs])))
  
command(name='kick', func=_kick)
