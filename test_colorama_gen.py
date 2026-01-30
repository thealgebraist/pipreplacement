import colorama
print(f'Successfully imported colorama')
try:
    import colorama.utils
    print('Successfully imported colorama.utils')
except ImportError: pass
