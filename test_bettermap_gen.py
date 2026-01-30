import bettermap
print(f'Successfully imported bettermap')
try:
    import bettermap.utils
    print('Successfully imported bettermap.utils')
except ImportError: pass
