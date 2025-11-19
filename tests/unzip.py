# Unzip all zip files "xxx.zip" in the current directory to folders "scenes/xxx" with the same name
import zipfile
import os

for item in os.listdir('.'):
    if item.endswith('.zip'):
        file_name = os.path.abspath(item)
        folder_name = os.path.join('scenes')
        with zipfile.ZipFile(file_name, 'r') as zip_ref:
            zip_ref.extractall(folder_name)

        print(f'Extracted {file_name} to {folder_name}')

print('All zip files have been extracted.')