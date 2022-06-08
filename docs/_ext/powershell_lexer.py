from pygments.lexers import PowerShellLexer


def setup(app):
    app.add_lexer('powershell', PowerShellLexer)
    return dict(parallel_read_safe=True)
