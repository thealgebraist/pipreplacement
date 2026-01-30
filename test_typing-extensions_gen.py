import typing-extensions
print(f'Successfully imported typing-extensions')
try:
    import typing-extensions.utils
    print('Successfully imported typing-extensions.utils')
except ImportError: pass
