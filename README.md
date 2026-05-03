# MegamanXLegacyCollectionROMLoader
ROM Loader for Mega Man X Legacy Collection

This .asi script hooks the internal ROM loading system used by the emulator and allows the game to load external .sfc ROM files from disk instead of only using the ROM embedded inside RXC1.exe.

Currently supported:
Mega Man X
Mega Man X2
Mega Man X3

# Install:
Drop the content of the zip file on the root folder (steamapps\common\Mega Man X Legacy Collection)
Drop RXC1.exe into "mmxlc_rom_extract_to_roms.py" to extract the roms
The extracted ROMs will be placed inside the "roms" folder
Replace them with your own ROMs if desired.
Enjoy

# Warning:
ROM files must be the exact expected size or smaller to work correctly.
Smaller ROMs are automatically padded with 00.
Larger ROMs are currently not supported due to internal emulator size limitations.
Maybe in future versions, i'll add support for bigger roms (Zero Project)

# Build requirements
Visual Studio
C++
MinHook (https://github.com/TsudaKageyu/minhook)
Also Python 3.14.4 for using the .py script

# Special Thanks:

s3phir0th115 for the original rom extractor script
Tsuda Kageyu for creating MinHook and facilitating the process.
NSA for creating Ghidra
