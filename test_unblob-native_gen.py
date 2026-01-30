import unblob_native
print(f'Successfully imported unblob_native')
try:
    import unblob_native.utils
    print('Successfully imported unblob_native.utils')
except ImportError: pass
