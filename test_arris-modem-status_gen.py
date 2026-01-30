import arris_modem_status
print(f'Successfully imported arris_modem_status')
try:
    import arris_modem_status.utils
    print('Successfully imported arris_modem_status.utils')
except ImportError: pass
