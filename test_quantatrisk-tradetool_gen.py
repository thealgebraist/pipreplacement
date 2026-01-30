import quantatrisk_tradetool
print(f'Successfully imported quantatrisk_tradetool')
try:
    import quantatrisk_tradetool.utils
    print('Successfully imported quantatrisk_tradetool.utils')
except ImportError: pass
