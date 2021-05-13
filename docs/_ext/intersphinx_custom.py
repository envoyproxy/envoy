#
# This module has been copied from sphinx.ext in order to workaround a
# sphinx issue described here:
#     https://github.com/sphinx-doc/sphinx/issues/2068
#
# using the fix from:
#     https://github.com/sphinx-doc/sphinx/pull/8981
#
# Once upstream is fixed this module should be removed.
#
# Envoy tracking bug is here:
#     https://github.com/envoyproxy/envoy/issues/16181
#
"""
    sphinx.ext.intersphinx
    ~~~~~~~~~~~~~~~~~~~~~~

    Insert links to objects documented in remote Sphinx documentation.

    This works as follows:

    * Each Sphinx HTML build creates a file named "objects.inv" that contains a
      mapping from object names to URIs relative to the HTML set's root.

    * Projects using the Intersphinx extension can specify links to such mapping
      files in the `intersphinx_mapping` config value.  The mapping will then be
      used to resolve otherwise missing references to objects into links to the
      other documentation.

    * By default, the mapping file is assumed to be at the same location as the
      rest of the documentation; however, the location of the mapping file can
      also be specified individually, e.g. if the docs should be buildable
      without Internet access.

    :copyright: Copyright 2007-2021 by the Sphinx team, see AUTHORS.
    :license: BSD, see LICENSE for details.
"""

import concurrent.futures
import functools
import posixpath
import sys
import time
from os import path
from typing import IO, Any, Dict, List, Tuple
from urllib.parse import urlsplit, urlunsplit

from docutils import nodes
from docutils.nodes import TextElement
from docutils.utils import relative_path

import sphinx
from sphinx.addnodes import pending_xref
from sphinx.application import Sphinx
from sphinx.builders.html import INVENTORY_FILENAME
from sphinx.config import Config
from sphinx.environment import BuildEnvironment
from sphinx.locale import _, __
from sphinx.util import logging, requests
from sphinx.util.inventory import InventoryFile
from sphinx.util.nodes import find_pending_xref_condition
from sphinx.util.typing import Inventory

logger = logging.getLogger(__name__)


class InventoryAdapter:
    """Inventory adapter for environment"""

    def __init__(self, env: BuildEnvironment) -> None:
        self.env = env

        if not hasattr(env, 'intersphinx_cache'):
            self.env.intersphinx_cache = {}  # type: ignore
            self.env.intersphinx_inventory = {}  # type: ignore
            self.env.intersphinx_named_inventory = {}  # type: ignore

    @property
    def cache(self) -> Dict[str, Tuple[str, int, Inventory]]:
        return self.env.intersphinx_cache  # type: ignore

    @property
    def main_inventory(self) -> Inventory:
        return self.env.intersphinx_inventory  # type: ignore

    @property
    def named_inventory(self) -> Dict[str, Inventory]:
        return self.env.intersphinx_named_inventory  # type: ignore

    def clear(self) -> None:
        self.env.intersphinx_inventory.clear()  # type: ignore
        self.env.intersphinx_named_inventory.clear()  # type: ignore


def _strip_basic_auth(url: str) -> str:
    """Returns *url* with basic auth credentials removed. Also returns the
    basic auth username and password if they're present in *url*.

    E.g.: https://user:pass@example.com => https://example.com

    *url* need not include basic auth credentials.

    :param url: url which may or may not contain basic auth credentials
    :type url: ``str``

    :return: *url* with any basic auth creds removed
    :rtype: ``str``
    """
    frags = list(urlsplit(url))
    # swap out "user[:pass]@hostname" for "hostname"
    if '@' in frags[1]:
        frags[1] = frags[1].split('@')[1]
    return urlunsplit(frags)


def _read_from_url(url: str, config: Config = None) -> IO:
    """Reads data from *url* with an HTTP *GET*.

    This function supports fetching from resources which use basic HTTP auth as
    laid out by RFC1738 § 3.1. See § 5 for grammar definitions for URLs.

    .. seealso:

       https://www.ietf.org/rfc/rfc1738.txt

    :param url: URL of an HTTP resource
    :type url: ``str``

    :return: data read from resource described by *url*
    :rtype: ``file``-like object
    """
    r = requests.get(url, stream=True, config=config, timeout=config.intersphinx_timeout)
    r.raise_for_status()
    r.raw.url = r.url
    # decode content-body based on the header.
    # ref: https://github.com/kennethreitz/requests/issues/2155
    r.raw.read = functools.partial(r.raw.read, decode_content=True)
    return r.raw


def _get_safe_url(url: str) -> str:
    """Gets version of *url* with basic auth passwords obscured. This function
    returns results suitable for printing and logging.

    E.g.: https://user:12345@example.com => https://user@example.com

    :param url: a url
    :type url: ``str``

    :return: *url* with password removed
    :rtype: ``str``
    """
    parts = urlsplit(url)
    if parts.username is None:
        return url
    else:
        frags = list(parts)
        if parts.port:
            frags[1] = '{}@{}:{}'.format(parts.username, parts.hostname, parts.port)
        else:
            frags[1] = '{}@{}'.format(parts.username, parts.hostname)

        return urlunsplit(frags)


def fetch_inventory(app: Sphinx, uri: str, inv: Any) -> Any:
    """Fetch, parse and return an intersphinx inventory file."""
    # both *uri* (base URI of the links to generate) and *inv* (actual
    # location of the inventory file) can be local or remote URIs
    localuri = '://' not in uri
    if not localuri:
        # case: inv URI points to remote resource; strip any existing auth
        uri = _strip_basic_auth(uri)
    try:
        if '://' in inv:
            f = _read_from_url(inv, config=app.config)
        else:
            f = open(path.join(app.srcdir, inv), 'rb')
    except Exception as err:
        err.args = ('intersphinx inventory %r not fetchable due to %s: %s',
                    inv, err.__class__, str(err))
        raise
    try:
        if hasattr(f, 'url'):
            newinv = f.url  # type: ignore
            if inv != newinv:
                logger.info(__('intersphinx inventory has moved: %s -> %s'), inv, newinv)

                if uri in (inv, path.dirname(inv), path.dirname(inv) + '/'):
                    uri = path.dirname(newinv)
        with f:
            try:
                join = path.join if localuri else posixpath.join
                invdata = InventoryFile.load(f, uri, join)
            except ValueError as exc:
                raise ValueError('unknown or unsupported inventory version: %r' % exc) from exc
    except Exception as err:
        err.args = ('intersphinx inventory %r not readable due to %s: %s',
                    inv, err.__class__.__name__, str(err))
        raise
    else:
        return invdata


def fetch_inventory_group(
    name: str, uri: str, invs: Any, cache: Any, app: Any, now: float
) -> bool:
    cache_time = now - app.config.intersphinx_cache_limit * 86400
    failures = []
    try:
        for inv in invs:
            if not inv:
                inv = posixpath.join(uri, INVENTORY_FILENAME)
            # decide whether the inventory must be read: always read local
            # files; remote ones only if the cache time is expired
            if '://' not in inv or uri not in cache or cache[uri][1] < cache_time:
                safe_inv_url = _get_safe_url(inv)
                logger.info(__('loading intersphinx inventory from %s...'), safe_inv_url)
                try:
                    invdata = fetch_inventory(app, uri, inv)
                except Exception as err:
                    failures.append(err.args)
                    continue
                if invdata:
                    cache[uri] = (name, now, invdata)
                    return True
        return False
    finally:
        if failures == []:
            pass
        elif len(failures) < len(invs):
            logger.info(__("encountered some issues with some of the inventories,"
                           " but they had working alternatives:"))
            for fail in failures:
                logger.info(*fail)
        else:
            issues = '\n'.join([f[0] % f[1:] for f in failures])
            logger.warning(__("failed to reach any of the inventories "
                              "with the following issues:") + "\n" + issues)


def load_mappings(app: Sphinx) -> None:
    """Load all intersphinx mappings into the environment."""
    now = int(time.time())
    inventories = InventoryAdapter(app.builder.env)

    with concurrent.futures.ThreadPoolExecutor() as pool:
        futures = []
        for name, (uri, invs) in app.config.intersphinx_mapping.values():
            futures.append(pool.submit(
                fetch_inventory_group, name, uri, invs, inventories.cache, app, now
            ))
        updated = [f.result() for f in concurrent.futures.as_completed(futures)]

    if any(updated):
        inventories.clear()

        # Duplicate values in different inventories will shadow each
        # other; which one will override which can vary between builds
        # since they are specified using an unordered dict.  To make
        # it more consistent, we sort the named inventories and then
        # add the unnamed inventories last.  This means that the
        # unnamed inventories will shadow the named ones but the named
        # ones can still be accessed when the name is specified.
        cached_vals = list(inventories.cache.values())
        named_vals = sorted(v for v in cached_vals if v[0])
        unnamed_vals = [v for v in cached_vals if not v[0]]
        for name, _x, invdata in named_vals + unnamed_vals:
            if name:
                inventories.named_inventory[name] = invdata
            for type, objects in invdata.items():
                inventories.main_inventory.setdefault(type, {}).update(objects)


def missing_reference(app: Sphinx, env: BuildEnvironment, node: pending_xref,
                      contnode: TextElement) -> nodes.reference:
    """Attempt to resolve a missing reference via intersphinx references."""
    target = node['reftarget']
    inventories = InventoryAdapter(env)
    objtypes: List[str] = None
    if node['reftype'] == 'any':
        # we search anything!
        objtypes = ['%s:%s' % (domain.name, objtype)
                    for domain in env.domains.values()
                    for objtype in domain.object_types]
        domain = None
    else:
        domain = node.get('refdomain')
        if not domain:
            # only objects in domains are in the inventory
            return None
        objtypes = env.get_domain(domain).objtypes_for_role(node['reftype'])
        if not objtypes:
            return None
        objtypes = ['%s:%s' % (domain, objtype) for objtype in objtypes]
    if 'std:cmdoption' in objtypes:
        # until Sphinx-1.6, cmdoptions are stored as std:option
        objtypes.append('std:option')
    if 'py:attribute' in objtypes:
        # Since Sphinx-2.1, properties are stored as py:method
        objtypes.append('py:method')

    # determine the contnode by pending_xref_condition
    content = find_pending_xref_condition(node, 'resolved')
    if content:
        # resolved condition found.
        contnodes = content.children
        contnode = content.children[0]  # type: ignore
    else:
        # not resolved. Use the given contnode
        contnodes = [contnode]

    to_try = [(inventories.main_inventory, target)]
    if domain:
        full_qualified_name = env.get_domain(domain).get_full_qualified_name(node)
        if full_qualified_name:
            to_try.append((inventories.main_inventory, full_qualified_name))
    in_set = None
    if ':' in target:
        # first part may be the foreign doc set name
        setname, newtarget = target.split(':', 1)
        if setname in inventories.named_inventory:
            in_set = setname
            to_try.append((inventories.named_inventory[setname], newtarget))
            if domain:
                node['reftarget'] = newtarget
                full_qualified_name = env.get_domain(domain).get_full_qualified_name(node)
                if full_qualified_name:
                    to_try.append((inventories.named_inventory[setname], full_qualified_name))
    elif app.config.intersphinx_strict_prefix:
        return None
    for inventory, target in to_try:
        for objtype in objtypes:
            if objtype not in inventory or target not in inventory[objtype]:
                continue
            proj, version, uri, dispname = inventory[objtype][target]
            if '://' not in uri and node.get('refdoc'):
                # get correct path in case of subdirectories
                uri = path.join(relative_path(node['refdoc'], '.'), uri)
            if version:
                reftitle = _('(in %s v%s)') % (proj, version)
            else:
                reftitle = _('(in %s)') % (proj,)
            newnode = nodes.reference('', '', internal=False, refuri=uri, reftitle=reftitle)
            if node.get('refexplicit'):
                # use whatever title was given
                newnode.extend(contnodes)
            elif dispname == '-' or \
                    (domain == 'std' and node['reftype'] == 'keyword'):
                # use whatever title was given, but strip prefix
                title = contnode.astext()
                if in_set and title.startswith(in_set + ':'):
                    newnode.append(contnode.__class__(title[len(in_set) + 1:],
                                                      title[len(in_set) + 1:]))
                else:
                    newnode.extend(contnodes)
            else:
                # else use the given display name (used for :ref:)
                newnode.append(contnode.__class__(dispname, dispname))
            return newnode
    # at least get rid of the ':' in the target if no explicit title given
    if in_set is not None and not node.get('refexplicit', True):
        if len(contnode) and isinstance(contnode[0], nodes.Text):
            contnode[0] = nodes.Text(newtarget, contnode[0].rawsource)

    return None


def normalize_intersphinx_mapping(app: Sphinx, config: Config) -> None:
    for key, value in config.intersphinx_mapping.copy().items():
        try:
            if isinstance(value, (list, tuple)):
                # new format
                name, (uri, inv) = key, value
                if not isinstance(name, str):
                    logger.warning(__('intersphinx identifier %r is not string. Ignored'),
                                   name)
                    config.intersphinx_mapping.pop(key)
                    continue
            else:
                # old format, no name
                name, uri, inv = None, key, value

            if not isinstance(inv, tuple):
                config.intersphinx_mapping[key] = (name, (uri, (inv,)))
            else:
                config.intersphinx_mapping[key] = (name, (uri, inv))
        except Exception as exc:
            logger.warning(__('Failed to read intersphinx_mapping[%s], ignored: %r'), key, exc)
            config.intersphinx_mapping.pop(key)


def setup(app: Sphinx) -> Dict[str, Any]:
    app.add_config_value('intersphinx_mapping', {}, True)
    app.add_config_value('intersphinx_cache_limit', 5, False)
    app.add_config_value('intersphinx_timeout', None, False)
    app.add_config_value('intersphinx_strict_prefix', True, True)
    app.connect('config-inited', normalize_intersphinx_mapping, priority=800)
    app.connect('builder-inited', load_mappings)
    app.connect('missing-reference', missing_reference)
    return {
        'version': sphinx.__display_version__,
        'env_version': 1,
        'parallel_read_safe': True
    }


def inspect_main(argv: List[str]) -> None:
    """Debug functionality to print out an inventory"""
    if len(argv) < 1:
        print("Print out an inventory file.\n"
              "Error: must specify local path or URL to an inventory file.",
              file=sys.stderr)
        sys.exit(1)

    class MockConfig:
        intersphinx_timeout: int = None
        intersphinx_strict_prefix: bool = True
        tls_verify = False
        user_agent = None

    class MockApp:
        srcdir = ''
        config = MockConfig()

        def warn(self, msg: str) -> None:
            print(msg, file=sys.stderr)

    try:
        filename = argv[0]
        invdata = fetch_inventory(MockApp(), '', filename)  # type: ignore
        for key in sorted(invdata or {}):
            print(key)
            for entry, einfo in sorted(invdata[key].items()):
                print('\t%-40s %s%s' % (entry,
                                        '%-40s: ' % einfo[3] if einfo[3] != '-' else '',
                                        einfo[2]))
    except ValueError as exc:
        print(exc.args[0] % exc.args[1:])
    except Exception as exc:
        print('Unknown error: %r' % exc)


if __name__ == '__main__':
    import logging as _logging
    _logging.basicConfig()

    inspect_main(argv=sys.argv[1:])
