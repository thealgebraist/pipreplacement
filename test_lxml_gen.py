import lxml
print(f'Successfully imported lxml')
try:
    import lxml.utils
    print('Successfully imported lxml.utils')
except ImportError: pass
