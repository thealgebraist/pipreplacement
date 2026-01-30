import flexstore
print(f'Successfully imported flexstore')
try:
    import flexstore.utils
    print('Successfully imported flexstore.utils')
except ImportError: pass
