use("github.com/repokitteh/modules/assign.star")
use("github.com/repokitteh/modules/review.star")
use("github.com/repokitteh/modules/wait.star")

def _kick(get_secret):
  github_issue_create_comment(get_secret('test'))
  
command(name="kick", func=_kick)
