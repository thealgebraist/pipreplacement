import certifi
print(f'Successfully imported certifi')
try:
    import certifi.utils
    print('Successfully imported certifi.utils')
except ImportError: pass
