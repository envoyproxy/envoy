from pygments.lexers import get_lexer_by_name, PowerShellLexer

def setup(app):
    app.add_lexer('powershell', PowerShellLexer)
