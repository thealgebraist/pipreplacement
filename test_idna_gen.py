import idna
print(f'Successfully imported idna')
try:
    import idna.utils
    print('Successfully imported idna.utils')
except ImportError: pass
