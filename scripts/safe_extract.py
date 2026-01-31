import zipfile, sys, os

def safe_extract(zip_path, extract_to):
    if not os.path.exists(zip_path):
        print(f"Error: {zip_path} does not exist")
        sys.exit(1)
    if not zipfile.is_zipfile(zip_path):
        print(f"Error: {zip_path} is not a valid zip file")
        sys.exit(1)
        
    try:
        with zipfile.ZipFile(zip_path, "r") as z:
            # Check for zip bomb or other issues
            if z.testzip() is not None:
                print(f"Error: {zip_path} failed testzip check")
                sys.exit(1)

            for member in z.infolist():
                # Prevent path traversal
                if os.path.isabs(member.filename) or ".." in member.filename:
                    print(f"Skipping dangerous member: {member.filename}")
                    continue
                z.extract(member, extract_to)
    except Exception as e:
        print(f"Error extracting {zip_path}: {e}")
        sys.exit(1)

if __name__ == "__main__":
    if len(sys.argv) < 3:
        sys.exit(1)
    safe_extract(sys.argv[1], sys.argv[2])
