import zyzz
print(f'Successfully imported zyzz')
try:
    import zyzz.utils
    print('Successfully imported zyzz.utils')
except ImportError: pass
