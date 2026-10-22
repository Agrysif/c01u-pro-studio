#pragma once

#define MS_VERSION_MAJOR 1
#define MS_VERSION_MINOR 1
#define MS_VERSION_PATCH 1
#define MS_VERSION_STR   "1.1.1"
#define MS_VERSION_WSTR  L"1.1.1"
#define MS_VERSION_RC    1,1,1,0

#define MS_PRODUCT_NAME   L"C01U Pro Studio"
#define MS_PRODUCT_NAME_A "C01U Pro Studio"
#define MS_PUBLISHER      L"C01U Pro Studio (unofficial)"
#define MS_COPYRIGHT      L"Unofficial driver for Samson C01U Pro. Not affiliated with Samson Technologies."

// GitHub repository with the releases the app updates itself from
#define MS_UPDATE_REPO    "Agrysif/c01u-pro-studio"

// Folder names (Program Files / ProgramData)
#define MS_DIR_NAME       L"C01U Pro Studio"
#define MS_EXE_NAME       L"C01UProStudio.exe"
#define MS_APO_DLL_NAME   L"C01UProStudioApo.dll"

// Registry
#define MS_REG_KEY        L"SOFTWARE\\C01U Pro Studio"
#define MS_RUN_VALUE      L"C01UProStudio"
#define MS_APP_ID         L"Agrysif.C01UProStudio"   // AppUserModelID: taskbar button and shortcuts
#define MS_UNINSTALL_KEY  L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\C01UProStudio"
