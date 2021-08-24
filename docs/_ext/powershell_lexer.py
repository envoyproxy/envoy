from pygments.lexers import PowerShellLexer


def setup(app):
    app.add_lexer('powershell', PowerShellLexer)
