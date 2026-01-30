import atomic
print(f'Successfully imported atomic')
try:
    import atomic.utils
    print('Successfully imported atomic.utils')
except ImportError: pass
