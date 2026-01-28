# This file contains intentionally problematic escape sequences to test spip auto-repair
path_with_d = "C:\data\test"  # \d is invalid
mixed = "Line with \s space"  # \s is invalid
escaped_bracket = "\[bracket\]"  # \[ is invalid
e_seq = "\e"  # \e is invalid
space_seq = "\ "  # \  is invalid
z_seq = "\Z"  # \Z is invalid

print("Script execution started")
print(path_with_d)
print(mixed)
print(escaped_bracket)
print(e_seq)
print(space_seq)
print(z_seq)
