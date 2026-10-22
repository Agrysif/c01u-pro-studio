#pragma once
#include <guiddef.h>

// CLSID of the system effect APO (SFX/LFX) that does the processing inside audiodg.exe
// {B51886B2-E6E1-4311-A6B2-DB9304D005F8}
#define MS_APO_CLSID_STR L"{B51886B2-E6E1-4311-A6B2-DB9304D005F8}"
static const GUID CLSID_MicStudioApo =
    {0xb51886b2, 0xe6e1, 0x4311, {0xa6, 0xb2, 0xdb, 0x93, 0x04, 0xd0, 0x05, 0xf8}};

// IID of IAudioSystemEffects, listed as the APO interface in the AudioEngine registration
#define MS_IID_AUDIO_SYSTEM_EFFECTS_STR L"{5FA00F27-ADD6-499a-8A9D-6B98521FA75B}"

// Property keys (format id + pid) used in MMDevices\...\FxProperties
#define MS_PKEY_FX_GUID              L"{d04e05a6-594b-4fb6-a80d-01af5eed7d1d}"
#define MS_PKEY_FX_PREMIX            MS_PKEY_FX_GUID L",1"   // LFX (Windows 8 / 8.1)
#define MS_PKEY_FX_POSTMIX           MS_PKEY_FX_GUID L",2"   // GFX (Windows 8 / 8.1)
#define MS_PKEY_FX_STREAM            MS_PKEY_FX_GUID L",5"   // SFX (Windows 10 / 11)
#define MS_PKEY_FX_MODE              MS_PKEY_FX_GUID L",6"   // MFX
#define MS_PKEY_FX_ENDPOINT          MS_PKEY_FX_GUID L",7"   // EFX
#define MS_PKEY_COMPOSITE_STREAM     MS_PKEY_FX_GUID L",13"  // composite SFX (Windows 11)
#define MS_PKEY_SFX_MODES            L"{d3993a3f-99c2-4402-b5ec-a92a0367664b},5"
#define MS_PKEY_DISABLE_SYSFX        L"{1da5d803-d492-4edd-8c23-e0c0ffee7f0e},5"

#define MS_AUDIO_MODE_DEFAULT_STR    L"{C18E2F7E-933D-4965-B7D1-1EEF228D2AF3}"
