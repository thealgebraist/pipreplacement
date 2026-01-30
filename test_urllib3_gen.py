import urllib3
print(f'Successfully imported urllib3')
try:
    import urllib3.utils
    print('Successfully imported urllib3.utils')
except ImportError: pass
