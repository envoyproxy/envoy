from pygments.lexers import get_lexer_by_name  # refer LEXERS


def setup(app):
    app.add_lexer('PowerShell', get_lexer_by_name('powershell'))
