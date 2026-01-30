import setuptools
print(f'Successfully imported setuptools')
try:
    import setuptools.utils
    print('Successfully imported setuptools.utils')
except ImportError: pass
