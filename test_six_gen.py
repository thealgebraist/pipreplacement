import six
print(f'Successfully imported six')
try:
    import six.utils
    print('Successfully imported six.utils')
except ImportError: pass
