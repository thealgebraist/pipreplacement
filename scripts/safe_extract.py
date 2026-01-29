import zipfile, sys, os


def safe_extract(zip_path, extract_to):
    with zipfile.ZipFile(zip_path, "r") as z:
        for member in z.infolist():
            # Prevent path traversal
            if os.path.isabs(member.filename) or ".." in member.filename:
                print(f"Skipping dangerous member: {member.filename}")
                continue
            z.extract(member, extract_to)


if __name__ == "__main__":
    if len(sys.argv) < 3:
        sys.exit(1)
    safe_extract(sys.argv[1], sys.argv[2])
