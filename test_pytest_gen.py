import pytest
print(f'Successfully imported pytest')
try:
    import pytest.utils
    print('Successfully imported pytest.utils')
except ImportError: pass
