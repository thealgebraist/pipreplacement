import petlib
print(f'Successfully imported petlib')
try:
    import petlib.utils
    print('Successfully imported petlib.utils')
except ImportError: pass
