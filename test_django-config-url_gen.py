import django-config-url
print(f'Successfully imported django-config-url')
try:
    import django-config-url.utils
    print('Successfully imported django-config-url.utils')
except ImportError: pass
