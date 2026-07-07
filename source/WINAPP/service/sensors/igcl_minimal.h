#pragma once
// Minimal Intel GPU Control Library (IGCL / Intel Arc Control) type definitions.
// DLL: ControlLib.dll — loaded via LoadLibraryW at runtime.
// Present when Intel Arc Control driver is installed (Arc dGPU, Xe iGPU with Arc drivers).
// Source: https://github.com/intel/drivers.gpu.control-library

typedef void* ctl_api_handle_t;
typedef void* ctl_device_adapter_handle_t;

// Return codes
typedef unsigned int ctl_result_t;
#define CTL_RESULT_SUCCESS             0
#define CTL_RESULT_ERROR_NOT_AVAILABLE 6

// Device type
typedef enum {
    CTL_DEVICE_TYPE_GRAPHICS = 1,
} ctl_device_type_t;

typedef struct {
    uint32_t Size;
    uint8_t  Version;
} ctl_base_interface_t;

typedef struct {
    uint32_t Size;       // must be sizeof(ctl_init_args_t)
    uint8_t  Version;
    uint32_t AppVersion;
    uint32_t flags;
    uint32_t SupportedVersion;
    char     ApplicationUID[64];
} ctl_init_args_t;

typedef struct {
    uint32_t Size;       // must be sizeof(ctl_device_adapter_properties_t)
    uint8_t  Version;
    char   name[64];
    char   driver_version[64];
    uint32_t pci_vendor_id;
    uint32_t pci_device_id;
    ctl_device_type_t device_type;
} ctl_device_adapter_properties_t;

// Telemetry data structure (simplified subset)
typedef struct {
    double GpuEnergyCounter;
    double GpuVoltage;
    double GpuCurrentClockFrequency;
    double GpuCurrentTemperature;
    double GlobalActivityCounter;    // GPU utilization %
    double RenderComputeActivityCounter;
    double MediaActivityCounter;
    double GpuPower;
    double VramReadBandwidthCounter;
    double VramWriteBandwidthCounter;
    double VramCurrentTemperature;
    double GpuCurrentVram;          // VRAM used in bytes
    double GpuMaxVram;              // VRAM total in bytes
} ctl_telemetry_data_t;

// Function pointer types
typedef ctl_result_t (*PfnCtlInit)(ctl_init_args_t*, ctl_api_handle_t*);
typedef ctl_result_t (*PfnCtlClose)(ctl_api_handle_t);
typedef ctl_result_t (*PfnCtlEnumerateDevices)(ctl_api_handle_t, uint32_t*, ctl_device_adapter_handle_t*);
typedef ctl_result_t (*PfnCtlGetDeviceProperties)(ctl_device_adapter_handle_t, ctl_device_adapter_properties_t*);
typedef ctl_result_t (*PfnCtlGetTelemetry)(ctl_device_adapter_handle_t, ctl_telemetry_data_t*);

struct IgclFuncs {
    PfnCtlInit              Init;
    PfnCtlClose             Close;
    PfnCtlEnumerateDevices  EnumerateDevices;
    PfnCtlGetDeviceProperties GetDeviceProperties;
    PfnCtlGetTelemetry      GetTelemetry;
};
