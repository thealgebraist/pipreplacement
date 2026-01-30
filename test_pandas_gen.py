import pandas
print(f'Successfully imported pandas')
try:
    import pandas.utils
    print('Successfully imported pandas.utils')
except ImportError: pass
