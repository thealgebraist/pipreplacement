import smash_cli
print(f'Successfully imported smash_cli')
try:
    import smash_cli.utils
    print('Successfully imported smash_cli.utils')
except ImportError: pass
