use("github.com/repokitteh/modules/assign.star")
use("github.com/repokitteh/modules/review.star")
use("github.com/repokitteh/modules/wait.star")

load('text', 'match')

def _kick(get_secret):
  state, statuses = github_get_statuses()
  
  statuses = [
    s for s in statuses 
    if s['context'].startswith('ci/circleci')
  ]
  
  print(statuses)
  
  jobs = []
  
  for status in statuses:
    m = match(text=status['target_url'], pattern='/([0-9]+)\?')
    if m and (len(m) == 2):
      jobs.append(m[1])
  
  github_issue_create_comment('%s %s' % (state, ','.join([str(j) for j in jobs])))
  
command(name="kick", func=_kick)
