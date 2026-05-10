# MegamanXLegacyCollectionROMLoader
ROM Loader for Mega Man X Legacy Collection

This .asi script hooks the internal ROM loading system used by the emulator and allows the game to load external .sfc ROM files from disk instead of only using the ROM embedded inside RXC1.exe.

Currently supported:<br/>
Rock Man X<br/>
Mega Man X<br/>
Rock Man X2<br/>
Mega Man X2<br/>
Rock Man X3<br/>
Mega Man X3<br/>

# Install:
Drop the content of the zip file on the root folder (steamapps\common\Mega Man X Legacy Collection)<br/>
Drop RXC1.exe into "mmxlc_rom_extract_to_roms.py" to extract the roms<br/>
The extracted ROMs will be placed inside the "roms" folder<br/>
Replace them with your own ROMs if desired.<br/>
Enjoy

# Build requirements
Visual Studio<br/>
C++<br/>
MinHook (https://github.com/TsudaKageyu/minhook)<br/>
Also Python 3.14.4 for using the .py script

# Special Thanks:

s3phir0th115 for the original rom extractor script<br/>
Tsuda Kageyu for creating MinHook and facilitating the process.<br/>
The Ghidra and xdbg teams
