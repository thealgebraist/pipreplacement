import boto3
print(f'Successfully imported boto3')
try:
    import boto3.utils
    print('Successfully imported boto3.utils')
except ImportError: pass
