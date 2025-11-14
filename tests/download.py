# Download test scenes from https://casual-effects.com/data/
import os
import urllib.request
import urllib.error
import time
from pathlib import Path

def download_file(url, dest_folder, retries=3, sleep_between=2):
    """Download a file with custom headers to avoid HTTP 406.

    Adds a browser-like User-Agent and Accept headers. Falls back to requests if available.
    """
    dest = Path(dest_folder)
    dest.mkdir(parents=True, exist_ok=True)
    filename = dest / url.split('/')[-1]
    print(f"Downloading {url} -> {filename}")

    headers = {
        "User-Agent": (
            "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
            "(KHTML, like Gecko) Chrome/122.0 Safari/537.36"
        ),
        "Accept": "*/*",
        "Accept-Language": "en-US,en;q=0.9",
        "Connection": "keep-alive",
        # Avoid gzip so we stream raw bytes easily
        "Accept-Encoding": "identity",
    }

    for attempt in range(1, retries + 1):
        try:
            req = urllib.request.Request(url, headers=headers)
            with urllib.request.urlopen(req, timeout=60) as resp, open(filename, "wb") as f:
                # Stream in chunks
                chunk_size = 1024 * 64
                total = 0
                while True:
                    chunk = resp.read(chunk_size)
                    if not chunk:
                        break
                    f.write(chunk)
                    total += len(chunk)
                print(f"Downloaded {total/1024/1024:.2f} MB -> {filename}")
            return filename
        except urllib.error.HTTPError as e:
            if e.code == 406:
                print(f"HTTP 406 Not Acceptable (attempt {attempt}/{retries}). Retrying with fallback...")
                # Try requests fallback if installed
                try:
                    import requests
                    r = requests.get(url, headers=headers, timeout=60)
                    if r.status_code == 200:
                        with open(filename, "wb") as f:
                            f.write(r.content)
                        print(f"Downloaded via requests fallback -> {filename}")
                        return filename
                    else:
                        print(f"Requests fallback status {r.status_code}")
                except ImportError:
                    print("'requests' not installed; skipping fallback.")
            else:
                print(f"HTTP error {e.code}: {e.reason} (attempt {attempt}/{retries})")
        except Exception as ex:
            print(f"Error: {ex} (attempt {attempt}/{retries})")
        if attempt < retries:
            time.sleep(sleep_between)

    raise RuntimeError(f"Failed to download {url} after {retries} attempts.")

def unzip_file(zip_path, extract_to):
    import zipfile
    print(f"Unzipping {zip_path} to {extract_to}...")
    with zipfile.ZipFile(zip_path, 'r') as zip_ref:
        zip_ref.extractall(extract_to)
    print("Unzip completed.")

if __name__ == "__main__":
    # Casual Effects archive sometimes rejects plain python user agents.
    urls = [
        "https://casual-effects.com/g3d/data10/common/model/CornellBox/CornellBox.zip",
        "https://casual-effects.com/g3d/data10/research/model/breakfast_room/breakfast_room.zip",
        "https://casual-effects.com/g3d/data10/research/model/conference/conference.zip",
    ]
    dest_folder = "tests/scenes"

    extract_paths = [
        Path(dest_folder) / "CornellBox",
        Path(dest_folder) / "breakfast_room",
        Path(dest_folder) / "conference",
    ]
    for url, extract_path in zip(urls, extract_paths):
        try:
            zip_path = download_file(url, dest_folder)
            unzip_file(str(zip_path), extract_path)
            os.remove(zip_path)
        except Exception as e:
            print(f"Failed to process {url}: {e}")

    print("All downloads attempted.")