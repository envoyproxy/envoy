# explicit import-as re-export so that mypy sees these types
# when implicit_reexport is disabled
#
# https://mypy.readthedocs.io/en/stable/config_file.html#confval-implicit_reexport
from .response import Response as Response
from .threading import request as request
from .threading import delete as delete
from .threading import get as get
from .threading import head as head
from .threading import options as options
from .threading import patch as patch
from .threading import post as post
from .threading import put as put
from .threading import trace as trace
