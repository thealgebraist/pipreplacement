import pytz
print(f'Successfully imported pytz')
try:
    import pytz.utils
    print('Successfully imported pytz.utils')
except ImportError: pass
