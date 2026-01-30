import uv
print(f'Successfully imported uv')
try:
    import uv.utils
    print('Successfully imported uv.utils')
except ImportError: pass
