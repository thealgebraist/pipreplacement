import auto-climate
print(f'Successfully imported auto-climate')
try:
    import auto-climate.utils
    print('Successfully imported auto-climate.utils')
except ImportError: pass
