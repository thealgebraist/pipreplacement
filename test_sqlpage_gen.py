import sqlpage
print(f'Successfully imported sqlpage')
try:
    import sqlpage.utils
    print('Successfully imported sqlpage.utils')
except ImportError: pass
