import registeringimages
print(f'Successfully imported registeringimages')
try:
    import registeringimages.utils
    print('Successfully imported registeringimages.utils')
except ImportError: pass
