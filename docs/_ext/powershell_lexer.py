from pygments.lexers import get_lexer_by_name  # refer LEXERS
from pygments.lexers._mapping import LEXERS
from pygments.lexers.python import PythonLexer


def setup(app):
    app.add_lexer('PowerShell', get_lexer_by_name('powershell'))
