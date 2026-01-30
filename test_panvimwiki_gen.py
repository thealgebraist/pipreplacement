import panvimwiki
print(f'Successfully imported panvimwiki')
try:
    import panvimwiki.utils
    print('Successfully imported panvimwiki.utils')
except ImportError: pass
