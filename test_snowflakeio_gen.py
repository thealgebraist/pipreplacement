import snowflakeio
print(f'Successfully imported snowflakeio')
try:
    import snowflakeio.utils
    print('Successfully imported snowflakeio.utils')
except ImportError: pass
