import soupsieve
print(f'Successfully imported soupsieve')
try:
    import soupsieve.utils
    print('Successfully imported soupsieve.utils')
except ImportError: pass
