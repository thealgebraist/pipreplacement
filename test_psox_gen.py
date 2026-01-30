import psox
print(f'Successfully imported psox')
try:
    import psox.utils
    print('Successfully imported psox.utils')
except ImportError: pass
