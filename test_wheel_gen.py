import wheel
print(f'Successfully imported wheel')
try:
    import wheel.utils
    print('Successfully imported wheel.utils')
except ImportError: pass
