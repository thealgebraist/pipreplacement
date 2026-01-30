import chardet
print(f'Successfully imported chardet')
try:
    import chardet.utils
    print('Successfully imported chardet.utils')
except ImportError: pass
