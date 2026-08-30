/* Minimal UEFI definitions for a freestanding bootloader. */
#ifndef PULSE_UEFI_H
#define PULSE_UEFI_H

#include "pulsebase.h"

/* All UEFI functions use the Microsoft x64 ABI on x86_64 builds.
   On IA-32, UEFI follows plain cdecl, so EFIAPI expands to nothing. */
#if defined(__x86_64__) || defined(_M_X64)
#define EFIAPI __attribute__((ms_abi))
#else
#define EFIAPI
#endif

typedef UINTN              EFI_STATUS;   /* spec: EFI_STATUS is UINTN-wide */

/* ---- status codes ---- */
#define EFI_SUCCESS               0
#define EFI_LOAD_ERROR            1
#define EFI_INVALID_PARAMETER     2
#define EFI_UNSUPPORTED           3
#define EFI_NOT_FOUND             14
#define EFI_BUFFER_TOO_SMALL      5
#define EFI_ERROR_BIT             0x8000000000000000ULL

#define EFIERR(x)   ((EFI_STATUS)(EFI_ERROR_BIT | (x)))
#define IS_ERROR(s) (((s) & EFI_ERROR_BIT) != 0)

/* ---- memory services ---- */
#define EFI_PAGE_SIZE   4096ULL
typedef UINT64 EFI_PHYSICAL_ADDRESS;

typedef enum {
    AllocateAnyPages,
    AllocateMaxAddress,
    AllocateAddress,
} EFI_ALLOCATE_TYPE;

typedef enum {
    EfiReservedMemoryType,
    EfiLoaderCode,
    EfiLoaderData,
    EfiBootServicesCode,
    EfiBootServicesData,
    EfiRuntimeServicesCode,
    EfiRuntimeServicesData,
    EfiConventionalMemory,
    EfiUnusableMemory,
    EfiACPIReclaimMemory,
    EfiACPIMemoryNVS,
    EfiMemoryMappedIO,
    EfiMemoryMappedIOPortSpace,
    EfiPalCode,
    EfiPersistentMemory,
} EFI_MEMORY_TYPE;

typedef struct {
    UINT32 Type;
    UINT32 Pad;
    UINT64 PhysicalStart;
    UINT64 VirtualStart;
    UINT64 NumberOfPages;
    UINT64 Attribute;
} EFI_MEMORY_DESCRIPTOR;

typedef EFIAPI EFI_STATUS (*EFI_ALLOCATE_PAGES_FN)(EFI_ALLOCATE_TYPE,
    EFI_MEMORY_TYPE, UINTN, EFI_PHYSICAL_ADDRESS *);
typedef EFIAPI EFI_STATUS (*EFI_ALLOCATE_POOL_FN)(EFI_MEMORY_TYPE, UINTN,
    VOID **);
typedef EFIAPI EFI_STATUS (*EFI_FREE_POOL_FN)(VOID *);
typedef EFIAPI EFI_STATUS (*EFI_GET_MEMORY_MAP_FN)(UINTN *, VOID *,
    UINTN *, UINTN *, UINT32 *);
typedef EFIAPI EFI_STATUS (*EFI_EXIT_BOOT_SERVICES_FN)(VOID *, UINTN);

/* ---- file I/O ---- */
typedef struct _EFI_FILE_PROTOCOL EFI_FILE_PROTOCOL;

typedef EFIAPI EFI_STATUS (*EFI_FILE_OPEN_FN)(EFI_FILE_PROTOCOL *,
    EFI_FILE_PROTOCOL **, CHAR16 *, UINT64, UINT64);
typedef EFIAPI EFI_STATUS (*EFI_FILE_READ_FN)(EFI_FILE_PROTOCOL *,
    UINTN *, VOID *);
typedef EFIAPI EFI_STATUS (*EFI_FILE_SETPOS_FN)(EFI_FILE_PROTOCOL *, UINT64);
typedef EFIAPI EFI_STATUS (*EFI_FILE_GETPOS_FN)(EFI_FILE_PROTOCOL *, UINT64 *);
typedef EFIAPI EFI_STATUS (*EFI_FILE_CLOSE_FN)(EFI_FILE_PROTOCOL *);

struct _EFI_FILE_PROTOCOL {
    UINT64            Revision;
    EFI_FILE_OPEN_FN  Open;
    EFI_FILE_CLOSE_FN Close;
    VOID              *Delete;
    EFI_FILE_READ_FN  Read;
    VOID              *Write;
    EFI_FILE_GETPOS_FN GetPosition;
    EFI_FILE_SETPOS_FN SetPosition;
    VOID              *GetInfo;
    VOID              *SetInfo;
    VOID              *Flush;
};

typedef EFIAPI EFI_STATUS (*EFI_OPEN_VOLUME_FN)(VOID *,
    EFI_FILE_PROTOCOL **);

typedef struct {
    UINT64             Revision;
    EFI_OPEN_VOLUME_FN OpenVolume;
} EFI_SIMPLE_FILE_SYSTEM_PROTOCOL;

#define EFI_FILE_MODE_READ 0x0000000000000001ULL

/* ---- GUID ---- */
typedef struct {
    UINT32 Data1;
    UINT16 Data2;
    UINT16 Data3;
    UINT8  Data4[8];
} EFI_GUID;

/* forward decls needed by the callback typedefs below */
typedef struct _EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL;
typedef struct _EFI_SIMPLE_INPUT_PROTOCOL EFI_SIMPLE_INPUT_PROTOCOL;

typedef EFIAPI EFI_STATUS (*EFI_LOCATE_PROTOCOL_FN)(EFI_GUID *, VOID *,
    VOID **);
typedef EFIAPI EFI_STATUS (*EFI_FILE_GETINFO_FN)(EFI_FILE_PROTOCOL *,
    EFI_GUID *, UINTN *, VOID *);
typedef EFIAPI EFI_STATUS (*EFI_TEXT_SETATTR_FN)(
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *, UINTN);
typedef EFIAPI EFI_STATUS (*EFI_TEXT_SETPOS_FN)(
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *, UINTN, UINTN);
typedef EFIAPI EFI_STATUS (*EFI_TEXT_CLEARSCREEN_FN)(
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *);
typedef struct { UINT16 ScanCode; CHAR16 UnicodeChar; } EFI_INPUT_KEY;
typedef EFIAPI EFI_STATUS (*EFI_READ_KEYSTROKE_FN)(
    EFI_SIMPLE_INPUT_PROTOCOL *, EFI_INPUT_KEY *);
typedef EFIAPI EFI_STATUS (*EFI_STALL_FN)(VOID *, UINTN);

/* scan codes we care about */
#define SCAN_NULL  0x00
#define SCAN_UP    0x01
#define SCAN_DOWN  0x02

/* ---- Simple Text Output protocol ---- */

typedef EFIAPI EFI_STATUS (*EFI_TEXT_STRING)(
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This,
    CHAR16 *String
);

struct _EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL {
    VOID  *Reset;
    EFI_TEXT_STRING OutputString;
    VOID  *TestString;
    VOID  *QueryMode;
    VOID  *SetMode;
    EFI_TEXT_CLEARSCREEN_FN ClearScreen;
    EFI_TEXT_SETATTR_FN SetAttribute;
    EFI_TEXT_SETPOS_FN SetPosition;
    VOID  *EnableCursor;
};

/* ---- Simple Input protocol ---- */
struct _EFI_SIMPLE_INPUT_PROTOCOL {
    VOID *Reset;
    EFI_READ_KEYSTROKE_FN ReadKeyStroke;
    VOID *WaitForKey;
};

/* ---- Table header for all UEFI tables ---- */
typedef struct {
    UINT64 Signature;
    UINT32 Revision;
    UINT32 HeaderSize;
    UINT32 CRC32;
    UINT32 Reserved;
} EFI_TABLE_HEADER;

/* ---- Boot Services: COMPLETE spec order (UEFI 2.10, Table 4.4).
   Every field must be present — omitting even one shifts all later
   offsets and makes firmware see the wrong function. Verified live
   against OVMF via slot probing (Reserved@160 faults as specified). */
typedef struct {
    EFI_TABLE_HEADER Hdr;                       /* 24 bytes */

    /* task priority services */
    VOID *RaiseTPL;
    VOID *RestoreTPL;

    /* memory services */
    EFI_ALLOCATE_PAGES_FN AllocatePages;
    VOID *FreePages;
    EFI_GET_MEMORY_MAP_FN GetMemoryMap;
    EFI_ALLOCATE_POOL_FN AllocatePool;
    EFI_FREE_POOL_FN FreePool;

    /* event & timer services */
    VOID *CreateEvent;
    VOID *SetTimer;
    VOID *WaitForEvent;
    VOID *SignalEvent;
    VOID *CloseEvent;
    VOID *CheckEvent;

    /* protocol handler services */
    VOID *InstallProtocolInterface;
    VOID *ReinstallProtocolInterface;
    VOID *UninstallProtocolInterface;
    VOID *HandleProtocol;
    VOID *Reserved;                             /* must fault if called */
    VOID *RegisterProtocolNotify;
    VOID *LocateHandle;
    VOID *LocateDevicePath;
    VOID *InstallConfigurationTable;

    /* image services */
    VOID *LoadImage;
    VOID *StartImage;
    VOID *Exit;
    VOID *UnloadImage;
    EFI_EXIT_BOOT_SERVICES_FN ExitBootServices;

    /* misc services */
    VOID *GetNextMonotonicCount;
    EFI_STALL_FN Stall;
    VOID *SetWatchdogTimer;

    /* controller & protocol-per-handle services */
    VOID *ConnectController;
    VOID *DisconnectController;
    VOID *OpenProtocol;
    VOID *CloseProtocol;
    VOID *OpenProtocolInformation;
    VOID *ProtocolsPerHandle;
    VOID *LocateHandleBuffer;
    EFI_LOCATE_PROTOCOL_FN LocateProtocol;
    VOID *InstallMultipleProtocolInterfaces;
    VOID *UninstallMultipleProtocolInterfaces;

    /* CRC + memory ops */
    VOID *CalculateCRC32;
    VOID *CopyMem;
    VOID *SetMem;
    VOID *CreateEventEx;
} EFI_BOOT_SERVICES;

/* ---- Runtime Services (we skip over this pointer) ---- */
typedef struct {
    EFI_TABLE_HEADER Hdr;
} EFI_RUNTIME_SERVICES;

/* ---- System Table ----
   NOTE: no explicit padding — natural alignment inserts 4 bytes after
   FirmwareRevision on x64 (before 8-aligned pointers) and none on IA-32,
   matching the EDK2 layouts on both architectures. */
typedef struct {
    UINT64 Signature;
    UINT32 Revision;
    UINT32 HeaderSize;
    UINT32 CRC32;
    UINT32 Reserved;

    VOID *FirmwareVendor;
    UINT32 FirmwareRevision;

    VOID  *ConsoleInHandle;
    EFI_SIMPLE_INPUT_PROTOCOL *ConIn;
    VOID  *ConsoleOutHandle;
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *ConOut;
    VOID  *StandardErrorHandle;
    VOID  *StdErr;

    EFI_RUNTIME_SERVICES *RuntimeServices;
    EFI_BOOT_SERVICES    *BootServices;

    UINTN NumberOfTableEntries;
    VOID *ConfigurationTable;
} EFI_SYSTEM_TABLE;

/* ---- image entry point ---- */
typedef EFIAPI EFI_STATUS (*EFI_IMAGE_ENTRY_POINT)(
    VOID *ImageHandle,
    EFI_SYSTEM_TABLE *SystemTable
);

/* ---- layout guards: fail the build if struct offsets drift ----
   UEFI IA-32 aligns UINT64 to 8 (MSVC style); gcc -m32 needs -malign-double
   to match. These asserts catch any regression at compile time. */
#define PULSE_OFFSET_OF(t, m)  ((UINTN)&(((t *)0)->m))

_Static_assert(sizeof(UINTN) == sizeof(void *), "UINTN must be pointer-width");
_Static_assert(PULSE_OFFSET_OF(EFI_TABLE_HEADER, Revision) == 8,
               "EFI_TABLE_HEADER: UINT64 must be 8-aligned");
#if defined(__x86_64__)
_Static_assert(PULSE_OFFSET_OF(EFI_BOOT_SERVICES, AllocatePages) == 24 + 2 * 8,
               "EFI_BOOT_SERVICES layout mismatch");
#else
_Static_assert(PULSE_OFFSET_OF(EFI_BOOT_SERVICES, AllocatePages) == 24 + 2 * 4,
               "EFI_BOOT_SERVICES layout mismatch");
#endif
_Static_assert(PULSE_OFFSET_OF(EFI_SYSTEM_TABLE, FirmwareVendor) == 24,
               "EFI_SYSTEM_TABLE layout mismatch after header");
#if defined(__x86_64__)
_Static_assert(PULSE_OFFSET_OF(EFI_SYSTEM_TABLE, ConOut) == 64,
               "EFI_SYSTEM_TABLE ConOut offset (x64)");
_Static_assert(PULSE_OFFSET_OF(EFI_SYSTEM_TABLE, BootServices) == 96,
               "EFI_SYSTEM_TABLE BootServices offset (x64)");
/* NOTE: this protocol has NO Revision field (unlike EFI_FILE_PROTOCOL) */
_Static_assert(PULSE_OFFSET_OF(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL,
                               SetPosition) == 7 * sizeof(VOID *),
               "ConOut SetPosition offset (x64)");
_Static_assert(PULSE_OFFSET_OF(EFI_BOOT_SERVICES, LocateProtocol) == 320,
               "EFI_BOOT_SERVICES LocateProtocol offset (x64)");
_Static_assert(PULSE_OFFSET_OF(EFI_BOOT_SERVICES, ExitBootServices) == 232,
               "EFI_BOOT_SERVICES ExitBootServices offset (x64)");
#else
_Static_assert(PULSE_OFFSET_OF(EFI_SYSTEM_TABLE, ConOut) == 44,
               "EFI_SYSTEM_TABLE ConOut offset (ia32)");
_Static_assert(PULSE_OFFSET_OF(EFI_SYSTEM_TABLE, BootServices) == 60,
               "EFI_SYSTEM_TABLE BootServices offset (ia32)");
_Static_assert(PULSE_OFFSET_OF(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL,
                               SetPosition) == 7 * sizeof(VOID *),
               "ConOut SetPosition offset (ia32)");
_Static_assert(PULSE_OFFSET_OF(EFI_BOOT_SERVICES, LocateProtocol) == 172,
               "EFI_BOOT_SERVICES LocateProtocol offset (ia32)");
_Static_assert(PULSE_OFFSET_OF(EFI_BOOT_SERVICES, ExitBootServices) == 128,
               "EFI_BOOT_SERVICES ExitBootServices offset (ia32)");
#endif

#endif
