import flask
print(f'Successfully imported flask')
try:
    import flask.utils
    print('Successfully imported flask.utils')
except ImportError: pass
