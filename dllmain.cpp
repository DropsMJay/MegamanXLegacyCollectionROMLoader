#include "pch.h"
#include <windows.h>
#include <cstdio>
#include <cstring>
#include "MinHook.h"

//Megaman X Legacy Collection ROM loader by MJay - 2026. Made with ChatGPT help.
//It replaces the internal ROM source with an external ROM file.
//Keeps fallback to the original internal ROM when no external ROM exists.
//Pad smaller ROM files with 00 up to the emulator map size.
//Notes: Debug thingies are still present but commented

//Supported files:
//- roms\rockmanx.sfc (Rock Man X)
//- roms\rockmanxu.sfc (Mega Man X)
//- roms\rockmanx2.sfc (Rock Man X2)
//- roms\rockmanx2u.sfc (Mega Man X2)
//- roms\rockmanx3.sfc (Rock Man X3)
//- roms\rockmanx3u.sfc (Mega Man X3)
//
//Notes:
//- Debug/investigation hooks were intentionally commented out instead of deleted.
//- The only active internal hook in this release is MapRomBanksToEmulatorMemory (FUN_00AD53C0).
//------------------------------------------------------------

#define RXC1_ENABLE_LOG 0 //Turn on/off debugging log

//------------------------------------------------------------
//Debug / investigation typedefs kept for reference.
//------------------------------------------------------------

/*
typedef HANDLE(WINAPI* CreateFileA_t)(
    LPCSTR lpFileName,
    DWORD dwDesiredAccess,
    DWORD dwShareMode,
    LPSECURITY_ATTRIBUTES lpSecurityAttributes,
    DWORD dwCreationDisposition,
    DWORD dwFlagsAndAttributes,
    HANDLE hTemplateFile
);

typedef HANDLE(WINAPI* CreateFileW_t)(
    LPCWSTR lpFileName,
    DWORD dwDesiredAccess,
    DWORD dwShareMode,
    LPSECURITY_ATTRIBUTES lpSecurityAttributes,
    DWORD dwCreationDisposition,
    DWORD dwFlagsAndAttributes,
    HANDLE hTemplateFile
);

typedef void(__thiscall* FUN_00EA6070_t)(void* ThisPointer, int param_2);
FUN_00EA6070_t Original_FUN_00EA6070 = nullptr;

typedef DWORD(__cdecl* FUN_00AD02F0_t)(
    const char* GameName,
    DWORD* OutLocalC,
    DWORD* OutLocal8,
    DWORD* OutLocal4
);
FUN_00AD02F0_t Original_FUN_00AD02F0 = nullptr;

CreateFileA_t OriginalCreateFileA = nullptr;
CreateFileW_t OriginalCreateFileW = nullptr;
*/

//FUN_00AD53C0
//Creates a reusable function pointer type for the original RXC1 internal function
//responsible for mapping ROM banks into emulator memory.
typedef DWORD(__cdecl* MapRomBanksToEmulatorMemory_t)(
    DWORD GameDriver, //Main game/emulator structure (current loaded game)
    BYTE* RomBase, //Pointer to the beginning of the loaded ROM data
    DWORD RomSize     //Total ROM size in bytes
    );

//Stores the original RXC1 function address before the hook redirects execution.
MapRomBanksToEmulatorMemory_t Original_MapRomBanksToEmulatorMemory = nullptr;

//Prevents recursive logging caused by CreateFile hooks calling WriteLogLine()
//which would otherwise call CreateFile again and create an infinite loop.
static __declspec(thread) bool IsLogging = false;

//Stores information for one external ROM slot used by the hook system.
struct ExternalRomSlot
{
    const char* GameName; //Internal RXC1 game identifier (example: rockmanxu)
    const char* FilePath; //External ROM file path to load from disk
    BYTE* Data; //Pointer to loaded ROM data stored in memory
    DWORD Size; //Loaded ROM size in bytes
    bool TriedLoading; //Prevents loading the same ROM multiple times
};
//Table of supported games and their matching external ROM files.
//If CurrentGameName matches GameName, this ROM will be loaded and used.
ExternalRomSlot ExternalRoms[] =
{
    { "rockmanx",   "roms\\rockmanx.sfc",   nullptr, 0, false },
    { "rockmanxu",  "roms\\rockmanxu.sfc",  nullptr, 0, false },
    { "rockmanx2",  "roms\\rockmanx2.sfc",  nullptr, 0, false },
    { "rockmanx2u", "roms\\rockmanx2u.sfc", nullptr, 0, false },
    { "rockmanx3",  "roms\\rockmanx3.sfc",  nullptr, 0, false },
    { "rockmanx3u", "roms\\rockmanx3u.sfc", nullptr, 0, false },
};

void WriteLogLine(const char* Text)
{
    //Prevents null text writes and avoids recursive logging loops
    //caused by CreateFile hooks calling this function again.
#if RXC1_ENABLE_LOG
    if (!Text || IsLogging)
        return;

    IsLogging = true; //Marks that logging is currently happening.

    HANDLE LogFile = CreateFileA(  //Writes the full text buffer into the log file.
        "RXC1_file_log.txt",
        FILE_APPEND_DATA,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr,
        OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );

    if (LogFile != INVALID_HANDLE_VALUE)  //If the file opened successfully, write the text to disk.
    {
        DWORD BytesWritten = 0;
        WriteFile(LogFile, Text, (DWORD)strlen(Text), &BytesWritten, nullptr);
        CloseHandle(LogFile);  //Closes the file handle after writing.
    }
    //Also sends the same text to the debugger output window
    //(useful when debugging with x64dbg / Visual Studio).
    OutputDebugStringA(Text);

    IsLogging = false; //Releases the recursion protection flag.
#else //Prevents compiler warnings when logging is disabled.
    (void)Text;
#endif
}


ExternalRomSlot* FindExternalRomSlot(const char* GameName) //Searches the external ROM table for a slot matching the current game name.
{
    if (!GameName) //If there is no game name, there is nothing to search for.
        return nullptr;

    for (int i = 0; i < (int)(sizeof(ExternalRoms) / sizeof(ExternalRoms[0])); i++)  //Loops through every supported external ROM entry.
    {
        if (strcmp(GameName, ExternalRoms[i].GameName) == 0) //Compares the current game name with this table entry.
            return &ExternalRoms[i]; //Returns the matching ROM slot.
    }

    return nullptr; //No matching game was found.
}

bool LoadExternalRomOnce(ExternalRomSlot* Slot, DWORD ExpectedSize) //Loads an external ROM file only once and stores it in memory.
{
    if (!Slot) //If there is no valid ROM slot, loading cannot continue.
        return false;

    //If this ROM was already checked before, reuse the previous result.
    //If Data is not null, the ROM was loaded successfully.
    if (Slot->TriedLoading)
        return Slot->Data != nullptr;

    //Marks this ROM as already attempted, so we do not keep reopening the file.
    Slot->TriedLoading = true;

    //Opens the external ROM file from the path stored in the ROM slot.
    HANDLE FileHandle = CreateFileA(
        Slot->FilePath, //ROM file path
        GENERIC_READ, //Open for reading only
        FILE_SHARE_READ, //Allow other programs to read it too
        nullptr, //Default security attributes
        OPEN_EXISTING, //Only open if the file exists
        FILE_ATTRIBUTE_NORMAL, //Normal file
        nullptr //No template file
    );
    //If CreateFileA failed, the external ROM does not exist or could not be opened.
    if (FileHandle == INVALID_HANDLE_VALUE)
    {
        char Buffer[512];
        sprintf_s(Buffer, "External ROM not found: %s\r\n", Slot->FilePath); //Creates a readable log message showing which ROM file was missing.
        WriteLogLine(Buffer);
        return false; //Loading failed, so the game will keep using the internal ROM.
    }

    //Gets the external ROM file size in bytes.
    DWORD FileSize = GetFileSize(FileHandle, nullptr);
    //FileHandle = Handle of the opened ROM file
    //nullptr = No high-order size needed (files are small enough for DWORD)

    //Gets the real size of the external ROM file from disk.
    //Formats a readable debug message showing:
    // - Which ROM file was opened
    // - The real file size from disk
    // - The original size expected by the emulator
    // Example:
    // External ROM file: roms\rockmanx3.sfc | Size: 00400000 | Expected/MapSize: 00200000
    char Buffer[512]; //Creates a temporary text buffer used to build the log message.
    sprintf_s(
        Buffer, //Destination text buffer
        "External ROM file: %s | Size: %08X | Expected/MapSize: %08X\r\n", //Debug text
        Slot->FilePath, //ROM file path being loaded
        FileSize, //Real ROM size found on disk
        ExpectedSize //Original ROM size expected by RXC1.exe
    );
    WriteLogLine(Buffer); //Writes the formatted message into RXC1_file_log.txt and also sends it to debugger output if logging is enabled.

    //If the external ROM is bigger than the emulator expects, reject it for now to avoid memory mapping problems or crashes.
    /*if (FileSize > ExpectedSize)
    {
        WriteLogLine("External ROM is larger than expected. Using internal ROM source.\r\n");
        CloseHandle(FileHandle); //Closes the file before leaving the function.
        return false; //Loading failed, so the original internal ROM will be used.
    }*/

    //Defines how much memory will be allocated for the ROM buffer.
    // Starts using the real external ROM size from disk.
    // Example:
    // If the ROM file is 4MB → AllocSize starts as 0x400000
    DWORD AllocSize = FileSize;
    //If the external ROM is smaller than the original internal ROM,
    //use the original expected size instead.
    //
    // This keeps compatibility with smaller ROMs by preserving the
    // emulator's original mapped size and automatically padding
    // the remaining space with 00 bytes.
    //
    // Example:
    // FileSize     = 0x180000
    // ExpectedSize = 0x200000
    //
    // Result:
    // AllocSize = 0x200000
    if (AllocSize < ExpectedSize)
        AllocSize = ExpectedSize;

    BYTE* BufferData = (BYTE*)VirtualAlloc( //Allocates memory in RAM for the external ROM. This creates a new buffer where the ROM will be loaded.
        nullptr, //let Windows choose the address
        AllocSize,//total size to allocate
        MEM_COMMIT | MEM_RESERVE,//reserve + make memory usable
        PAGE_READWRITE//readable and writable memory
    );

    if (!BufferData) //Checks if VirtualAlloc failed.
    {
        WriteLogLine("VirtualAlloc failed for external ROM.\r\n");
        CloseHandle(FileHandle);
        return false;
    }

    //VirtualAlloc returns zero-filled memory, so smaller ROMs are automatically padded with 00.
    DWORD BytesRead = 0;
    BOOL ReadOk = ReadFile(
        FileHandle, //Open ROM file handle
        BufferData, //Destination memory buffer
        FileSize, //Real ROM file size
        &BytesRead, //Returns how many bytes were actually read
        nullptr //No overlapped/asynchronous reading
    );

    CloseHandle(FileHandle); //Closes the ROM file after reading it.

    if (!ReadOk || BytesRead != FileSize)  //Checks if ReadFile failed or read fewer bytes than expected.
    {
        WriteLogLine("ReadFile failed for external ROM.\r\n");
        VirtualFree(BufferData, 0, MEM_RELEASE); //Frees the allocated ROM buffer because loading failed.
        return false; //Loading failed, so the game will use the internal ROM.
    }

    Slot->Data = BufferData; //Saves the loaded ROM buffer inside this game's ROM slot.
    Slot->Size = AllocSize; //Stores the padded/mapped ROM size, not only the file size.

    //Creates a success log after the external ROM was fully loaded.
    sprintf_s(
        Buffer,
        "External ROM loaded successfully for %s. Read=%08X | PaddedTo=%08X\r\n",
        Slot->GameName,
        FileSize,
        AllocSize
    );
    WriteLogLine(Buffer);

    return true;//Loading succeeded, so the hook can use this ROM buffer.
}

//------------------------------------------------------------
//Debug / Investigation hooks kept commented for future reference.
//------------------------------------------------------------

/*
HANDLE WINAPI HookedCreateFileA(...)
{
    //File-path logging hook used during investigation.
}

HANDLE WINAPI HookedCreateFileW(...)
{
    //File-path logging hook used during investigation.
}

DWORD __cdecl Hooked_FUN_00AD02F0(...)
{
    //Game-name / ROM pointer logging hook used during investigation.
}

void __fastcall Hooked_FUN_00EA6070(...)
{
    //Game selection / VTable logging hook used during investigation.
}
*/

//Hook for FUN_00AD53C0 (MapRomBanksToEmulatorMemory).
//This is the most important hook in the project:
//it replaces the original internal ROM source with the external ROM file.
DWORD __cdecl Hooked_MapRomBanksToEmulatorMemory(
    DWORD GameDriver, //Main emulator/game structure
    BYTE* RomBase, //Original internal ROM pointer
    DWORD RomSize //Expected ROM size used by the emulator
)
{
    HMODULE GameModule = GetModuleHandleA(nullptr); //Gets the RXC1.exe module base address.
    DWORD BaseAddress = (DWORD)GameModule;

    //Points to the internal string that stores the currently loaded game name.
    //Example: rockmanxu / rockmanx2u / rockmanx3u
    const char** CurrentGameNamePointer = (const char**)(BaseAddress + 0x016E319C);
    const char* CurrentGameName = nullptr;

    //Safely reads the current game name.
    if (CurrentGameNamePointer && *CurrentGameNamePointer) // If the pointer is valid, store the current game name.
        CurrentGameName = *CurrentGameNamePointer;

    char Buffer[512]; //Temporary text buffer used to build the log message.
    sprintf_s( //Creates a debug log showing:
        Buffer,
        "MapRomBanksToEmulatorMemory | Game=%s | GameDriver=%08X | RomBase=%08X | RomSize=%08X\r\n",
        CurrentGameName ? CurrentGameName : "(null)", //Current game name
        GameDriver, //Main emulator/game structure
        (DWORD)RomBase, //Original internal ROM pointer
        RomSize //Original expected ROM size
    );
    WriteLogLine(Buffer); //Writes the message to the log file.

    ExternalRomSlot* Slot = FindExternalRomSlot(CurrentGameName); //Searches for a matching external ROM slot for this game.

    if (Slot && LoadExternalRomOnce(Slot, RomSize)) //If a valid external ROM was found and loaded successfully, prepare to replace the internal ROM from RXC1.exe.
    {
        sprintf_s( //Writes in the debug log
            Buffer,
            "Replacing internal ROM source with external ROM: %s\r\n",
            Slot->FilePath //External ROM file path
        );
        WriteLogLine(Buffer);

        //Calls the original RXC1 function, but now using the external ROM
        //loaded from disk instead of the internal ROM embedded in the .exe.

        sprintf_s(
            Buffer,
            "MapRomBanksToEmulatorMemory called | InternalRomBase=%08X | ExternalRomBase=%08X | OriginalRomSize=%08X | ExternalSize=%08X\r\n",
            (DWORD)RomBase, //Original internal ROM pointer from RXC1.exe
            (DWORD)Slot->Data, //External ROM buffer loaded from disk
            RomSize, //Original emulator expected ROM size
            Slot->Size //Final size used (supports expanded ROMs)
        );
        WriteLogLine(Buffer); //Writes the debug message to the log file.

        return Original_MapRomBanksToEmulatorMemory(
            GameDriver, //Same emulator/game structure
            Slot->Data, //External ROM buffer loaded in memory
            Slot->Size //Final ROM size (supports expanded ROMs)
            //RomSize //Original expected emulator ROM size
        );
    }

    //If no valid external ROM was found,
    //fallback to the original internal ROM from RXC1.exe.
    WriteLogLine("Using internal ROM source.\r\n");

    return Original_MapRomBanksToEmulatorMemory( //Calls the original function normally using the original ROM source.
        GameDriver, //Same emulator/game structure
        RomBase, //Original ROM embedded inside RXC1.exe
        RomSize //Original expected ROM size
    );
}

//DLL entry point.
//This runs automatically when the DLL is injected into RXC1.exe.
BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID)
{
    //Only run initialization when the DLL is first attached
    //to the game process.
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(hModule); //Prevents unnecessary thread notifications to improve stability.

        MH_Initialize(); //Initializes the MinHook library before creating hooks.

        HMODULE GameModule = GetModuleHandleA(nullptr); //Gets the RXC1.exe base module address in memory.
        DWORD BaseAddress = (DWORD)GameModule;

        //============================================================
        //   Old debug hooks kept for future investigation
        //   Used during reverse engineering and file-path discovery
        //============================================================
        /*
        MH_CreateHookApi(
            L"kernel32.dll",
            "CreateFileA",
            &HookedCreateFileA,
            reinterpret_cast<LPVOID*>(&OriginalCreateFileA)
        );

        MH_CreateHookApi(
            L"kernel32.dll",
            "CreateFileW",
            &HookedCreateFileW,
            reinterpret_cast<LPVOID*>(&OriginalCreateFileW)
        );

        LPVOID Target_FUN_00EA6070 = (LPVOID)(BaseAddress + 0x00AA6070);
        MH_CreateHook(
            Target_FUN_00EA6070,
            &Hooked_FUN_00EA6070,
            reinterpret_cast<LPVOID*>(&Original_FUN_00EA6070)
        );

        LPVOID Target_FUN_00AD02F0 = (LPVOID)(BaseAddress + 0x006D02F0);
        MH_CreateHook(
            Target_FUN_00AD02F0,
            &Hooked_FUN_00AD02F0,
            reinterpret_cast<LPVOID*>(&Original_FUN_00AD02F0)
        );
        */

        //Calculates the runtime address of FUN_00AD53C0.
        //0x006D53C0 is the RVA: 0x00AD53C0 - 0x00400000.
        LPVOID Target_MapRomBanksToEmulatorMemory = (LPVOID)(BaseAddress + 0x006D53C0);
        //Creates the hook that redirects RXC1's ROM bank mapping function
        //to our Hooked_MapRomBanksToEmulatorMemory function.
        MH_CreateHook(
            Target_MapRomBanksToEmulatorMemory, //Original RXC1 function address
            &Hooked_MapRomBanksToEmulatorMemory, //Our replacement hook function
            reinterpret_cast<LPVOID*>(&Original_MapRomBanksToEmulatorMemory) //Stores original function
        );

        MH_EnableHook(MH_ALL_HOOKS); //Enables every hook created above.

        WriteLogLine("RXC1 external ROM loader loaded.\r\n"); //Optional log message confirming the loader was initialized.
    }

    if (reason == DLL_PROCESS_DETACH) //Runs when the DLL is unloaded from RXC1.exe.
    {
        MH_DisableHook(MH_ALL_HOOKS); //Disables all active MinHook hooks.
        MH_Uninitialize(); //Shuts down MinHook.

        for (int i = 0; i < (int)(sizeof(ExternalRoms) / sizeof(ExternalRoms[0])); i++) //Frees any external ROM buffers that were allocated with VirtualAlloc.
        {
            if (ExternalRoms[i].Data) //If this slot has loaded ROM data, free it.
            {
                VirtualFree(ExternalRoms[i].Data, 0, MEM_RELEASE);
                //Clears the slot so it no longer points to freed memory.
                ExternalRoms[i].Data = nullptr;
                ExternalRoms[i].Size = 0;
            }
        }
    }

    return TRUE; //Tells Windows that the DLL loaded/unloaded successfully.
}
