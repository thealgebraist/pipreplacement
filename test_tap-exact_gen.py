import tap_exact
print(f'Successfully imported tap_exact')
try:
    import tap_exact.utils
    print('Successfully imported tap_exact.utils')
except ImportError: pass
