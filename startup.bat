timeout 30
start "host" "C:\Users\Kaegans Booth of Joy\Desktop\coinhost.bat"
start "obs" "C:\Users\Kaegans Booth of Joy\Desktop\OBS Studio.lnk"
start "Booth" "C:\Users\Kaegans Booth of Joy\Desktop\dslrBooth.lnk"
timeout 45
nircmd win activate process "dslrBooth.exe"