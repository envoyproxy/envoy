# -*- coding: utf-8 -*-
#
# envoy documentation build configuration file, created by
# sphinx-quickstart on Sat May 28 10:51:27 2016.
#
# This file is execfile()d with the current directory set to its
# containing dir.
#
# Note that not all possible configuration values are present in this
# autogenerated file.
#
# All configuration values have a default; values that are commented out
# serve to show the default.

import os
import sys
from datetime import datetime

import yaml

from sphinx.directives.code import CodeBlock
import sphinx_rtd_theme


class SphinxConfigError(Exception):
    pass


# https://stackoverflow.com/questions/44761197/how-to-use-substitution-definitions-with-code-blocks
class SubstitutionCodeBlock(CodeBlock):
    """
  Similar to CodeBlock but replaces placeholders with variables. See "substitutions" below.
  """

    def run(self):
        """
    Replace placeholders with given variables.
    """
        app = self.state.document.settings.env.app
        new_content = []
        existing_content = self.content
        for item in existing_content:
            for pair in app.config.substitutions:
                original, replacement = pair
                item = item.replace(original, replacement)
            new_content.append(item)

        self.content = new_content
        return list(CodeBlock.run(self))


def setup(app):
    app.add_config_value('release_level', '', 'env')
    app.add_config_value('substitutions', [], 'html')
    app.add_directive('substitution-code-block', SubstitutionCodeBlock)


missing_config = (
    not os.environ.get("ENVOY_DOCS_BUILD_CONFIG")
    or not os.path.exists(os.environ["ENVOY_DOCS_BUILD_CONFIG"]))

if missing_config:
    raise SphinxConfigError(
        "`ENVOY_DOCS_BUILD_CONFIG` env var must be defined, "
        "and point to a valid yaml file")

with open(os.environ["ENVOY_DOCS_BUILD_CONFIG"]) as f:
    configs = yaml.safe_load(f.read())


def _config(key):
    if not configs.get(key):
        raise SphinxConfigError(f"`{key}` config var must be defined")
    return configs[key]


# If extensions (or modules to document with autodoc) are in another directory,
# add these directories to sys.path here. If the directory is relative to the
# documentation root, use os.path.abspath to make it absolute, like shown here.
#sys.path.insert(0, os.path.abspath('.'))

# -- General configuration ------------------------------------------------

# If your documentation needs a minimal Sphinx version, state it here.
#needs_sphinx = '1.0'

# Add any Sphinx extension module names here, as strings. They can be
# extensions coming with Sphinx (named 'sphinx.ext.*') or your custom
# ones.

sys.path.append(os.path.abspath("./_ext"))

extensions = [
    'sphinxcontrib.httpdomain', 'sphinx.ext.extlinks', 'sphinx.ext.ifconfig',
    'sphinx.ext.intersphinx', 'sphinx_tabs.tabs', 'sphinx_copybutton', 'validating_code_block',
    'sphinxext.rediraffe', 'powershell_lexer'
]

release_level = _config('release_level')
blob_sha = _config('blob_sha')

extlinks = {
    'repo': ('https://github.com/envoyproxy/envoy/blob/{}/%s'.format(blob_sha), ''),
    'api': ('https://github.com/envoyproxy/envoy/blob/{}/api/%s'.format(blob_sha), ''),
}

# Only lookup intersphinx for explicitly prefixed in cross-references
# This makes docs versioning work
intersphinx_disabled_domains = ['std']

# Setup global substitutions
if 'pre-release' in release_level:
    substitutions = [
        ('|envoy_docker_image|', 'envoy-dev:{}'.format(blob_sha)),
        ('|envoy_windows_docker_image|', 'envoy-windows-dev:{}'.format(blob_sha)),
        ('|envoy_distroless_docker_image|', 'envoy-distroless-dev:{}'.format(blob_sha))
    ]
else:
    substitutions = [('|envoy_docker_image|', 'envoy:{}'.format(blob_sha)),
                     ('|envoy_windows_docker_image|', 'envoy-windows:{}'.format(blob_sha)),
                     ('|envoy_distroless_docker_image|', 'envoy-distroless:{}'.format(blob_sha))]

# Add any paths that contain templates here, relative to this directory.
templates_path = ['_templates']

copybutton_prompt_text = r"\$ |PS>"
copybutton_prompt_is_regexp = True

# The suffix(es) of source filenames.
# You can specify multiple suffix as a list of string:
# source_suffix = ['.rst', '.md']
source_suffix = '.rst'

# The encoding of source files.
#source_encoding = 'utf-8-sig'

# The master toctree document.
master_doc = 'index'

# General information about the project.
project = u'envoy'
copyright = u'2016-{}, Envoy Project Authors'.format(datetime.now().year)
author = u'Envoy Project Authors'

# The version info for the project you're documenting, acts as replacement for
# |version| and |release|, also used in various other places throughout the
# built documents.

# The short X.Y version.
version = _config('version_string')
# The full version, including alpha/beta/rc tags.
release = _config('version_string')

rst_epilog = """
.. |DOCKER_IMAGE_TAG_NAME| replace:: {}
""".format(_config('docker_image_tag_name'))

# The language for content autogenerated by Sphinx. Refer to documentation
# for a list of supported languages.
#
# This is also used if you do content translation via gettext catalogs.
# Usually you set "language" from the command line for these cases.
language = None

# There are two options for replacing |today|: either, you set today to some
# non-false value, then it is used:
#today = ''
# Else, today_fmt is used as the format for a strftime call.
#today_fmt = '%B %d, %Y'

# List of patterns, relative to source directory, that match files and
# directories to ignore when looking for source files.
# This patterns also effect to html_static_path and html_extra_path
exclude_patterns = [
    '_build',
    '_venv',
    'Thumbs.db',
    '.DS_Store',
]

# The reST default role (used for this markup: `text`) to use for all
# documents.
#default_role = None

# If true, '()' will be appended to :func: etc. cross-reference text.
#add_function_parentheses = True

# If true, the current module name will be prepended to all description
# unit titles (such as .. function::).
#add_module_names = True

# If true, sectionauthor and moduleauthor directives will be shown in the
# output. They are ignored by default.
#show_authors = False

# The name of the Pygments (syntax highlighting) style to use.
#pygments_style = 'sphinx'

# A list of ignored prefixes for module index sorting.
#modindex_common_prefix = []

# If true, keep warnings as "system message" paragraphs in the built documents.
#keep_warnings = False

# If true, `todo` and `todoList` produce output, else they produce nothing.
todo_include_todos = False

# -- Options for HTML output ----------------------------------------------

# The theme to use for HTML and HTML Help pages.  See the documentation for
# a list of builtin themes.
html_theme = 'sphinx_rtd_theme'

# Theme options are theme-specific and customize the look and feel of a theme
# further.  For a list of options available for each theme, see the
# documentation.
html_theme_options = {
    'logo_only': True,
    'includehidden': False,
}

# Add any paths that contain custom themes here, relative to this directory.
html_theme_path = [sphinx_rtd_theme.get_html_theme_path()]

# The name for this set of Sphinx documents.
# "<project> v<release> documentation" by default.
#html_title = u'envoy v1.0.0'

# A shorter title for the navigation bar.  Default is the same as html_title.
#html_short_title = None

# The name of an image file (relative to this directory) to place at the top
# of the sidebar.
html_logo = 'img/envoy-logo.png'

# The name of an image file (relative to this directory) to use as a favicon of
# the docs.  This file should be a Windows icon file (.ico) being 16x16 or 32x32
# pixels large.
html_favicon = 'favicon.ico'

# Add any paths that contain custom static files (such as style sheets) here,
# relative to this directory. They are copied after the builtin static files,
# so a file named "default.css" will overwrite the builtin "default.css".
html_static_path = ['_static']

html_style = 'css/envoy.css'

# Add any extra paths that contain custom files (such as robots.txt or
# .htaccess) here, relative to this directory. These files are copied
# directly to the root of the documentation.
#html_extra_path = []

# If not None, a 'Last updated on:' timestamp is inserted at every page
# bottom, using the given strftime format.
# The empty string is equivalent to '%b %d, %Y'.
#html_last_updated_fmt = None

# If true, SmartyPants will be used to convert quotes and dashes to
# typographically correct entities.
#html_use_smartypants = True

# Custom sidebar templates, maps document names to template names.
#html_sidebars = {}

# Additional templates that should be rendered to pages, maps page names to
# template names.
#html_additional_pages = {}

# If false, no module index is generated.
#html_domain_indices = True

# If false, no index is generated.
#html_use_index = True

# If true, the index is split into individual pages for each letter.
#html_split_index = False

# If true, links to the reST sources are added to the pages.
#html_show_sourcelink = True

# If true, "Created using Sphinx" is shown in the HTML footer. Default is True.
#html_show_sphinx = True

# If true, "(C) Copyright ..." is shown in the HTML footer. Default is True.
#html_show_copyright = True

# If true, an OpenSearch description file will be output, and all pages will
# contain a <link> tag referring to it.  The value of this option must be the
# base URL from which the finished HTML is served.
#html_use_opensearch = ''

# This is the file name suffix for HTML files (e.g. ".xhtml").
#html_file_suffix = None

# Language to be used for generating the HTML full-text search index.
# Sphinx supports the following languages:
#   'da', 'de', 'en', 'es', 'fi', 'fr', 'hu', 'it', 'ja'
#   'nl', 'no', 'pt', 'ro', 'ru', 'sv', 'tr', 'zh'
#html_search_language = 'en'

# A dictionary with options for the search language support, empty by default.
# 'ja' uses this config value.
# 'zh' user can custom change `jieba` dictionary path.
#html_search_options = {'type': 'default'}

# The name of a javascript file (relative to the configuration directory) that
# implements a search results scorer. If empty, the default will be used.
#html_search_scorer = 'scorer.js'

# Output file base name for HTML help builder.
htmlhelp_basename = 'envoydoc'

# TODO(phlax): add redirect diff (`rediraffe_branch` setting)
#  - not sure how diffing will work with main merging in PRs - might need
#    to be injected dynamically, somehow
rediraffe_redirects = "envoy-redirects.txt"

intersphinx_mapping = {
    'v1.5': ('https://www.envoyproxy.io/docs/envoy/v1.5.0', "inventories/v1.5/objects.inv"),
    'v1.6': ('https://www.envoyproxy.io/docs/envoy/v1.6.0', "inventories/v1.6/objects.inv"),
    'v1.7': ('https://www.envoyproxy.io/docs/envoy/v1.7.1', "inventories/v1.7/objects.inv"),
    'v1.8': ('https://www.envoyproxy.io/docs/envoy/v1.8.0', "inventories/v1.8/objects.inv"),
    'v1.9': ('https://www.envoyproxy.io/docs/envoy/v1.9.1', "inventories/v1.9/objects.inv"),
    'v1.10': ('https://www.envoyproxy.io/docs/envoy/v1.10.0', "inventories/v1.10/objects.inv"),
    'v1.11': ('https://www.envoyproxy.io/docs/envoy/v1.11.2', "inventories/v1.11/objects.inv"),
    'v1.12': ('https://www.envoyproxy.io/docs/envoy/v1.12.6', "inventories/v1.12/objects.inv"),
    'v1.13': ('https://www.envoyproxy.io/docs/envoy/v1.13.3', "inventories/v1.13/objects.inv"),
    'v1.14': ('https://www.envoyproxy.io/docs/envoy/v1.14.7', "inventories/v1.14/objects.inv"),
    'v1.15': ('https://www.envoyproxy.io/docs/envoy/v1.15.5', "inventories/v1.15/objects.inv"),
    'v1.16': ('https://www.envoyproxy.io/docs/envoy/v1.16.5', "inventories/v1.16/objects.inv"),
    'v1.17': ('https://www.envoyproxy.io/docs/envoy/v1.17.4', "inventories/v1.17/objects.inv"),
    'v1.18': ('https://www.envoyproxy.io/docs/envoy/v1.18.4', "inventories/v1.18/objects.inv"),
    'v1.19': ('https://www.envoyproxy.io/docs/envoy/v1.19.4', "inventories/v1.19/objects.inv"),
    'v1.20': ('https://www.envoyproxy.io/docs/envoy/v1.20.1', "inventories/v1.20/objects.inv"),
    'v1.21': ('https://www.envoyproxy.io/docs/envoy/v1.21.1', "inventories/v1.21/objects.inv"),
    'v1.22': ('https://www.envoyproxy.io/docs/envoy/v1.22.0', "inventories/v1.22/objects.inv"),
}
