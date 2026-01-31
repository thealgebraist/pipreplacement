pkgs="p1 p2 p3"
timeout 10m bash -c '
  for pkg in $1; do
    echo "Testing: $pkg"
  done
' bash "$pkgs"
