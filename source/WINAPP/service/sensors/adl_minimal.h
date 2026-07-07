#pragma once
// Minimal AMD Display Library (ADL2) type definitions for dynamic loading.
// DLL: atiadlxx.dll (64-bit) — loaded via LoadLibraryW at runtime.
// Source: AMD ADL SDK public headers.

typedef void* ADL_CONTEXT_HANDLE;
typedef void* (*ADL_MAIN_MALLOC_CALLBACK)(int);

// ADL return codes
#define ADL_OK              0
#define ADL_OK_WARNING      1
#define ADL_ERR            -1
#define ADL_ERR_NOT_INIT   -2
#define ADL_ERR_NO_DEVICE  -6

typedef struct AdapterInfo {
    int  iSize;
    int  iAdapterIndex;
    char strUDID[256];
    int  iBusNumber;
    int  iDeviceNumber;
    int  iFunctionNumber;
    int  iVendorID;
    char strAdapterName[256];
    char strDisplayName[256];
    int  iPresent;
    int  iExist;
    char strDriverPath[256];
    char strDriverPathExt[256];
    char strPNPString[256];
    int  iOSDisplayIndex;
} AdapterInfo;

typedef struct ADLTemperature {
    int iSize;
    int iTemperature;  // in millidegrees C (divide by 1000)
} ADLTemperature;

typedef struct ADLFanSpeedValue {
    int iSize;
    int iSpeedType;
    int iFanSpeed;
    int iFlags;
} ADLFanSpeedValue;
#define ADL_DL_FANCTRL_SPEED_TYPE_PERCENT 1

typedef struct ADLPMActivity {
    int iSize;
    int iEngineClock;   // in 10 kHz units
    int iMemoryClock;   // in 10 kHz units
    int iVddc;
    int iActivityPercent;
    int iCurrentPerformanceLevel;
    int iCurrentBusSpeed;
    int iCurrentBusLanes;
    int iMaximumBusLanes;
    int iReserved;
} ADLPMActivity;

typedef struct ADLMemoryInfo {
    long long iMemorySize;
    char      strMemoryType[256];
    long long iMemoryBandwidth;
} ADLMemoryInfo;

// Function pointer types
typedef int  (*PfnADL2_Main_Control_Create)(ADL_MAIN_MALLOC_CALLBACK, int, ADL_CONTEXT_HANDLE*);
typedef int  (*PfnADL2_Main_Control_Destroy)(ADL_CONTEXT_HANDLE);
typedef int  (*PfnADL2_Adapter_NumberOfAdapters_Get)(ADL_CONTEXT_HANDLE, int*);
typedef int  (*PfnADL2_Adapter_AdapterInfo_Get)(ADL_CONTEXT_HANDLE, AdapterInfo*, int);
typedef int  (*PfnADL2_Adapter_Active_Get)(ADL_CONTEXT_HANDLE, int, int*);
typedef int  (*PfnADL2_Overdrive5_Temperature_Get)(ADL_CONTEXT_HANDLE, int, int, ADLTemperature*);
typedef int  (*PfnADL2_Overdrive5_FanSpeed_Get)(ADL_CONTEXT_HANDLE, int, int, ADLFanSpeedValue*);
typedef int  (*PfnADL2_Overdrive5_CurrentActivity_Get)(ADL_CONTEXT_HANDLE, int, ADLPMActivity*);
typedef int  (*PfnADL2_Adapter_MemoryInfo_Get)(ADL_CONTEXT_HANDLE, int, ADLMemoryInfo*);

struct AdlFuncs {
    PfnADL2_Main_Control_Create           Create;
    PfnADL2_Main_Control_Destroy          Destroy;
    PfnADL2_Adapter_NumberOfAdapters_Get  GetAdapterCount;
    PfnADL2_Adapter_AdapterInfo_Get       GetAdapterInfo;
    PfnADL2_Adapter_Active_Get            GetAdapterActive;
    PfnADL2_Overdrive5_Temperature_Get    GetTemp;
    PfnADL2_Overdrive5_FanSpeed_Get       GetFan;
    PfnADL2_Overdrive5_CurrentActivity_Get GetActivity;
    PfnADL2_Adapter_MemoryInfo_Get        GetMemInfo;
};

// ADL malloc callback (required by ADL2_Main_Control_Create)
inline void* __cdecl adl_malloc(int size) {
    return malloc(size);
}
