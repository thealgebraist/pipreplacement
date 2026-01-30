import python-dateutil
print(f'Successfully imported python-dateutil')
try:
    import python-dateutil.utils
    print('Successfully imported python-dateutil.utils')
except ImportError: pass
