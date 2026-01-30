import six
print(f"six version: {six.__version__}")
assert isinstance(six.string_types, tuple)
print("Assertion passed.")
