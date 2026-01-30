import flask_dbkit
print(f'Successfully imported flask_dbkit')
try:
    import flask_dbkit.utils
    print('Successfully imported flask_dbkit.utils')
except ImportError: pass
