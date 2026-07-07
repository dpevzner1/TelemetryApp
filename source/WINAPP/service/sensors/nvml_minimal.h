#pragma once
// Minimal NVML type definitions for dynamic loading — no CUDA toolkit required.
// Function pointers resolved via LoadLibraryW("nvml.dll") at runtime.
// Source: https://docs.nvidia.com/deploy/nvml-api/nvml_8h_source.html

typedef void*    nvmlDevice_t;
typedef unsigned int nvmlReturn_t;

// nvmlReturn_t values we check
#define NVML_SUCCESS                    0
#define NVML_ERROR_UNINITIALIZED        1
#define NVML_ERROR_NOT_SUPPORTED        3
#define NVML_ERROR_INSUFFICIENT_SIZE    7
#define NVML_ERROR_UNKNOWN             999

// Clock types
typedef enum { NVML_CLOCK_GRAPHICS = 0, NVML_CLOCK_SM = 1,
               NVML_CLOCK_MEM = 2, NVML_CLOCK_VIDEO = 3 } nvmlClockType_t;

// Temperature sensor
typedef enum { NVML_TEMPERATURE_GPU = 0 } nvmlTemperatureSensors_t;

// Throttle reason bitmask constants
#define NVML_CLOCKS_THROTTLE_REASON_SW_POWER_CAP       0x0000000000000004ULL
#define NVML_CLOCKS_THROTTLE_REASON_HW_SLOWDOWN        0x0000000000000008ULL
#define NVML_CLOCKS_THROTTLE_REASON_SW_THERMAL_SLOWDOWN 0x0000000000000020ULL
#define NVML_CLOCKS_THROTTLE_REASON_HW_THERMAL_SLOWDOWN 0x0000000000000040ULL
#define NVML_CLOCKS_THROTTLE_REASON_HW_POWER_BRAKE_SLOWDOWN 0x0000000000000080ULL

typedef struct {
    unsigned long long total;
    unsigned long long free;
    unsigned long long used;
} nvmlMemory_t;

typedef struct {
    unsigned int gpu;
    unsigned int memory;
} nvmlUtilization_t;

typedef struct {
    unsigned int  pid;
    unsigned long long usedGpuMemory;
} nvmlProcessInfo_t;

typedef struct {
    unsigned int pid;
    unsigned long long timeStamp;
    unsigned int smUtil;
    unsigned int memUtil;
    unsigned int encUtil;
    unsigned int decUtil;
} nvmlProcessUtilizationSample_t;

// Function pointer typedefs — one per NVML call we use
typedef nvmlReturn_t (*PfnNvmlInit)(void);
typedef nvmlReturn_t (*PfnNvmlShutdown)(void);
typedef nvmlReturn_t (*PfnNvmlDeviceGetCount)(unsigned int*);
typedef nvmlReturn_t (*PfnNvmlDeviceGetHandleByIndex)(unsigned int, nvmlDevice_t*);
typedef nvmlReturn_t (*PfnNvmlDeviceGetName)(nvmlDevice_t, char*, unsigned int);
typedef nvmlReturn_t (*PfnNvmlDeviceGetMemoryInfo)(nvmlDevice_t, nvmlMemory_t*);
typedef nvmlReturn_t (*PfnNvmlDeviceGetUtilizationRates)(nvmlDevice_t, nvmlUtilization_t*);
typedef nvmlReturn_t (*PfnNvmlDeviceGetTemperature)(nvmlDevice_t, nvmlTemperatureSensors_t, unsigned int*);
typedef nvmlReturn_t (*PfnNvmlDeviceGetPowerUsage)(nvmlDevice_t, unsigned int*);
typedef nvmlReturn_t (*PfnNvmlDeviceGetClockInfo)(nvmlDevice_t, nvmlClockType_t, unsigned int*);
typedef nvmlReturn_t (*PfnNvmlDeviceGetFanSpeed)(nvmlDevice_t, unsigned int*);
typedef nvmlReturn_t (*PfnNvmlDeviceGetCurrentClocksThrottleReasons)(nvmlDevice_t, unsigned long long*);
typedef nvmlReturn_t (*PfnNvmlDeviceGetComputeRunningProcesses)(nvmlDevice_t, unsigned int*, nvmlProcessInfo_t*);
typedef nvmlReturn_t (*PfnNvmlDeviceGetGraphicsRunningProcesses)(nvmlDevice_t, unsigned int*, nvmlProcessInfo_t*);
typedef nvmlReturn_t (*PfnNvmlDeviceGetProcessUtilization)(nvmlDevice_t, nvmlProcessUtilizationSample_t*, unsigned int*, unsigned long long);
typedef nvmlReturn_t (*PfnNvmlDeviceGetEncoderUtilization)(nvmlDevice_t, unsigned int*, unsigned int*);
typedef nvmlReturn_t (*PfnNvmlDeviceGetDecoderUtilization)(nvmlDevice_t, unsigned int*, unsigned int*);
typedef nvmlReturn_t (*PfnNvmlDeviceGetCudaComputeCapability)(nvmlDevice_t, int*, int*);

// Grouped function table — resolved once at init
struct NvmlFuncs {
    PfnNvmlInit                              Init;
    PfnNvmlShutdown                          Shutdown;
    PfnNvmlDeviceGetCount                    GetCount;
    PfnNvmlDeviceGetHandleByIndex            GetHandle;
    PfnNvmlDeviceGetName                     GetName;
    PfnNvmlDeviceGetMemoryInfo               GetMemInfo;
    PfnNvmlDeviceGetUtilizationRates         GetUtil;
    PfnNvmlDeviceGetTemperature              GetTemp;
    PfnNvmlDeviceGetPowerUsage               GetPower;
    PfnNvmlDeviceGetClockInfo                GetClock;
    PfnNvmlDeviceGetFanSpeed                 GetFan;
    PfnNvmlDeviceGetCurrentClocksThrottleReasons GetThrottle;
    PfnNvmlDeviceGetComputeRunningProcesses  GetComputeProcs;
    PfnNvmlDeviceGetGraphicsRunningProcesses GetGraphicsProcs;
    PfnNvmlDeviceGetProcessUtilization       GetProcUtil;
    PfnNvmlDeviceGetEncoderUtilization       GetEncoder;
    PfnNvmlDeviceGetDecoderUtilization       GetDecoder;
    PfnNvmlDeviceGetCudaComputeCapability    GetCudaCC;
};
