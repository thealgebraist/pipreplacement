import httpcore
print(f'Successfully imported httpcore')
try:
    import httpcore.utils
    print('Successfully imported httpcore.utils')
except ImportError: pass
