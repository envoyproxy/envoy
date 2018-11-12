use("github.com/repokitteh/modules/assign.star")
use("github.com/repokitteh/modules/review.star")
use("github.com/repokitteh/modules/wait.star")

def _kick():
  github_issue_create_comment('kick')
  
command(name="kick", func=_kick)
