import zarr_jpeg2k
print(f'Successfully imported zarr_jpeg2k')
try:
    import zarr_jpeg2k.utils
    print('Successfully imported zarr_jpeg2k.utils')
except ImportError: pass
