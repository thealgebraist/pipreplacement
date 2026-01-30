import serial-communicator
print(f'Successfully imported serial-communicator')
try:
    import serial-communicator.utils
    print('Successfully imported serial-communicator.utils')
except ImportError: pass
