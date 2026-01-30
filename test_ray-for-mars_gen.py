import ray_for_mars
print(f'Successfully imported ray_for_mars')
try:
    import ray_for_mars.utils
    print('Successfully imported ray_for_mars.utils')
except ImportError: pass
