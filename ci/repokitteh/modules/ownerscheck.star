# Ownership specified by list of specs, like so:
#
# use(
#   "github.com/repokitteh/modules/ownerscheck.star",
#   paths=[
#     {
#       "owner": "envoyproxy/api-shepherds!",
#       "path": "api/",
#       "label": "api",
#     },
#   ],
# )
#
# This module will maintain a commit status per specified path (also aka as spec).
#
# Two types of approvals:
# 1. Global approvals, done by approving the PR using Github's review approval feature.
# 2. Partial approval, done by commenting "/lgtm [label]" where label is the label
#    associated with the path. This does not affect GitHub's PR approve status, only
#    this module's maintained commit status. This approval is automatically revoked
#    if any further changes are done to the relevant files in this spec.

load("github.com/repokitteh/modules/lib/utils.star", "react")

def _store_partial_approval(who, files):
  for f in files:
    store_put('ownerscheck/partial/%s:%s' % (who, f['filename']), f['sha'])


def _is_partially_approved(who, files):
  for f in files:
    sha = store_get('ownerscheck/partial/%s:%s' % (who, f['filename']))
    if sha != f['sha']:
      return False

  return True


def _get_relevant_specs(specs, changed_files):
  if not specs:
    print("no specs")
    return []

  relevant = []

  for spec in specs:
    prefix = spec["path"]

    files = [f for f in changed_files if f['filename'].startswith(prefix)]
    if files:
      relevant.append(struct(files=files, prefix=prefix, **spec))

  print("specs: %s" % relevant)

  return relevant


def _get_global_approvers(): # -> List[str] (owners)
  reviews = [{'login': r['user']['login'], 'state': r['state']} for r in github.pr_list_reviews()]

  print("reviews=%s" % reviews)

  return [r['login'] for r in reviews if r['state'] == 'APPROVED']


def _is_approved(spec, approvers):
  owner = spec.owner

  if owner[-1] == '!':
    owner = owner[:-1]

  required = [owner]

  if '/' in owner:
    team_name = owner.split('/')[1]

    # this is a team, parse it.
    team_id = github.team_get_by_name(team_name)['id']
    required = [m['login'] for m in github.team_list_members(team_id)]

    print("team %s(%d) = %s" % (team_name, team_id, required))

  for r in required:
    if any([a for a in approvers if a == r]):
      print("global approver: %s" % r)
      return True

    if _is_partially_approved(r, spec.files):
      print("partial approval: %s" % r)
      return True

  return False


def _update_status(owner, prefix, approved):
  github.create_status(
    state=approved and 'success' or 'pending',
    context='%s must approve' % owner,
    description='changes to %s' % (prefix or '/'),
  )

def _get_specs(config):
  return _get_relevant_specs(config.get('paths', []), github.pr_list_files())

def _reconcile(config, specs=None):
  specs = specs or _get_specs(config)

  if not specs:
    return []

  approvers = _get_global_approvers()

  print("approvers: %s" % approvers)

  results = []

  for spec in specs:
    approved = _is_approved(spec, approvers)

    print("%s -> %s" % (spec, approved))

    results.append((spec, approved))

    if spec.owner[-1] == '!':
      _update_status(spec.owner[:-1], spec.prefix, approved)

      if hasattr(spec, 'label'):
        if approved:
          github.issue_unlabel(spec.label)
        else:
          github.issue_label(spec.label)
    elif hasattr(spec, 'label'): # fyis
      github.issue_label(spec.label)

  return results


def _comment(config, results, force=False):
  lines = []

  for spec, approved in results:
    if approved:
      continue

    mention = spec.owner

    if mention[0] != '@':
      mention = '@' + mention

    if mention[-1] == '!':
      mention = mention[:-1]

    prefix = spec.prefix
    if prefix:
      prefix = ' for changes made to `' + prefix + '`'

    mode = spec.owner[-1] == '!' and 'approval' or 'fyi'

    key = "ownerscheck/%s/%s" % (spec.owner, spec.prefix)

    if (not force) and (store_get(key) == mode):
      mode = 'skip'
    else:
      store_put(key, mode)

    if mode == 'approval':
      lines.append('CC %s: Your approval is needed%s.' % (mention, prefix))
    elif mode == 'fyi':
      lines.append('CC %s: FYI only%s.' % (mention, prefix))

  if lines:
    github.issue_create_comment('\n'.join(lines))


def _reconcile_and_comment(config):
  _comment(config, _reconcile(config))


def _force_reconcile_and_comment(config):
  _comment(config, _reconcile(config), force=True)


def _pr(action, config):
  if action in ['synchronize', 'opened']:
    _reconcile_and_comment(config)


def _pr_review(action, review_state, config):
  if action != 'submitted' or not review_state:
    return

  _reconcile(config)


# Partial approvals are done by commenting "/lgtm [label]".
def _lgtm_by_comment(config, comment_id, command, sender, sha):
  labels = command.args

  if len(labels) != 1:
    react(comment_id, 'please specify a single label can be specified')
    return

  label = labels[0]

  specs = [s for s in _get_specs(config) if hasattr(s, 'label') and s.label == label]

  if len(specs) == 0:
    react(comment_id, 'no relevant owners for "%s"' % label)
    return

  for spec in specs:
    _store_partial_approval(sender, spec.files)

  react(comment_id, None)

  _reconcile(config, specs)


handlers.pull_request(func=_pr)
handlers.pull_request_review(func=_pr_review)

handlers.command(name='checkowners', func=_reconcile)
handlers.command(name='checkowners!', func=_force_reconcile_and_comment)
handlers.command(name='lgtm', func=_lgtm_by_comment)
