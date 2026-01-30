import django
print(f'Successfully imported django')
try:
    import django.utils
    print('Successfully imported django.utils')
except ImportError: pass
