use("github.com/repokitteh/modules/assign.star")
use("github.com/repokitteh/modules/review.star")
use("github.com/repokitteh/modules/wait.star")

load('text', 'match')

def _kick(get_secret):
  _, statuses = github_get_statuses()
  
  statuses = [
    s for s in statuses 
    if (s['context'] == 'ci/circleci')
  ]
  
  print(statuses)
  
  for status in statuses[1]:
    m = match(text=status['target_url'], pattern='/[0-9]+\?')
    print(m)
  
  github_issue_create_comment(get_secret('test'))
  
command(name="kick", func=_kick)
