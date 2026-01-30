import zzarpdf
print(f'Successfully imported zzarpdf')
try:
    import zzarpdf.utils
    print('Successfully imported zzarpdf.utils')
except ImportError: pass
