import devautomator
print(f'Successfully imported devautomator')
try:
    import devautomator.utils
    print('Successfully imported devautomator.utils')
except ImportError: pass
