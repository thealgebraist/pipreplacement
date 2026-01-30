import nvidia_cuda_runtime_cu11
print(f'Successfully imported nvidia_cuda_runtime_cu11')
try:
    import nvidia_cuda_runtime_cu11.utils
    print('Successfully imported nvidia_cuda_runtime_cu11.utils')
except ImportError: pass
