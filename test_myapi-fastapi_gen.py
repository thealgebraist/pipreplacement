import myapi_fastapi
print(f'Successfully imported myapi_fastapi')
try:
    import myapi_fastapi.utils
    print('Successfully imported myapi_fastapi.utils')
except ImportError: pass
