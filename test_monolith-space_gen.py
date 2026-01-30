import monolith-space
print(f'Successfully imported monolith-space')
try:
    import monolith-space.utils
    print('Successfully imported monolith-space.utils')
except ImportError: pass
