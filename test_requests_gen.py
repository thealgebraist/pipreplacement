import requests
print(f'Successfully imported requests')
try:
    import requests.utils
    print('Successfully imported requests.utils')
except ImportError: pass
