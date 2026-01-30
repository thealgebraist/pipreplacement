import fastapi
print(f'Successfully imported fastapi')
try:
    import fastapi.utils
    print('Successfully imported fastapi.utils')
except ImportError: pass
