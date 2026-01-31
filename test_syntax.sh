pkgs="gremlinpython langgraph-prebuilt ua-parser s3transfer"
echo "Starting 10 minute stress test on: $pkgs"

# Ensure telemetry dir exists
mkdir -p ${HOME}/.spip/telemetry

# Use timeout to ensure we stay within 10 minutes total
# Use SPIP_NO_TMPFS=1 to avoid sudo mount issues in CI
export SPIP_NO_TMPFS=1

timeout 10m bash -c '
  for pkg in "$@"; do
    echo "----------------------------------------"
    echo "🧪 Testing: $pkg"
    # limit to 2 versions per pkg to ensure we finish 4 pkgs in 10m
    # use --threads 4 for parallel version testing
    ./spip matrix "$pkg" --limit 2 --threads 4 --telemetry
  done
' -- $pkgs || echo "Test reached 10 minute timeout"
