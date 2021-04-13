from pygments.lexers import get_lexer_by_name


def setup(app):
    app.add_lexer('powershell', get_lexer_by_name('powershell'))
