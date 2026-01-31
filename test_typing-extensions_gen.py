import typing_extensions
print(f'Successfully imported typing_extensions')
try:
    import typing_extensions.utils
    print('Successfully imported typing_extensions.utils')
except ImportError: pass
