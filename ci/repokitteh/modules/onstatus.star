
def _status(config):
  data = dict(called_by="STATUS")
  github.issue_create_comment(data)


handlers.status(func=_status)


def _push(config):
  data = dict(called_by="PUSH")
  github.issue_create_comment(data)


handlers.push(func=_push)


def _pull_request(action, config):
  data = dict(called_by="PR", action=action)
  github.issue_create_comment(data)


# handlers.pull_request(func=_pull_request)
