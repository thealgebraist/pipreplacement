import jpaccident
print(f'Successfully imported jpaccident')
try:
    import jpaccident.utils
    print('Successfully imported jpaccident.utils')
except ImportError: pass
