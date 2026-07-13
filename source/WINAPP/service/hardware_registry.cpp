#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dxgi1_2.h>
#include <intrin.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "hardware_registry.h"
#include "../shared/shm_layout.h"
#include "../shared/metric_ids.h"

#pragma comment(lib, "dxgi.lib")

using json = nlohmann::json;

namespace Service {
namespace {

std::string WideToUtf8(const wchar_t* w) {
    if (!w || !*w) return {};
    int needed = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    if (needed <= 1) return {};
    std::string out(static_cast<size_t>(needed - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, -1, &out[0], needed, nullptr, nullptr);
    return out;
}

std::string RegString(HKEY root, const wchar_t* path, const wchar_t* name) {
    HKEY hk{};
    if (RegOpenKeyExW(root, path, 0, KEY_READ | KEY_WOW64_64KEY, &hk) != ERROR_SUCCESS)
        return {};
    DWORD type = 0;
    DWORD bytes = 0;
    if (RegQueryValueExW(hk, name, nullptr, &type, nullptr, &bytes) != ERROR_SUCCESS ||
        (type != REG_SZ && type != REG_EXPAND_SZ) || bytes < sizeof(wchar_t)) {
        RegCloseKey(hk);
        return {};
    }
    std::wstring buf(bytes / sizeof(wchar_t), L'\0');
    if (RegQueryValueExW(hk, name, nullptr, nullptr,
                         reinterpret_cast<LPBYTE>(&buf[0]), &bytes) != ERROR_SUCCESS) {
        RegCloseKey(hk);
        return {};
    }
    RegCloseKey(hk);
    if (!buf.empty() && buf.back() == L'\0') buf.pop_back();
    return WideToUtf8(buf.c_str());
}

std::string VendorFromPciId(UINT vendor_id) {
    switch (vendor_id) {
    case 0x10DE: return "NVIDIA";
    case 0x1002:
    case 0x1022: return "AMD";
    case 0x8086: return "Intel";
    case 0x1414: return "Microsoft";
    default: return "Unknown";
    }
}

std::string Hex4(UINT v) {
    char buf[16]{};
    std::snprintf(buf, sizeof(buf), "0x%04X", v & 0xFFFFu);
    return buf;
}

std::string GpuProviderForVendor(const std::string& vendor) {
    if (vendor == "NVIDIA") return "NVML + DXGI";
    if (vendor == "AMD") return "ADL current path; AMD SMI/ADLX recommended for mature power/energy";
    if (vendor == "Intel") return "IGCL current path; Level Zero Sysman recommended for mature power/energy";
    return "DXGI/PDH fallback";
}

std::string QualityFromValue(double v) {
    return v > 0.0 ? "measured" : "unavailable";
}

json Capability(bool supported,
                const char* quality,
                const char* status,
                const char* source,
                const char* reason = "") {
    return {
        {"supported", supported},
        {"quality", quality},
        {"status", status},
        {"source", source},
        {"reason", reason}
    };
}

void Cpuid(int regs[4], int leaf, int subleaf = 0) {
    __cpuidex(regs, leaf, subleaf);
}

bool CpuFeatureLeaf7(int bit, int reg_index) {
    int r[4]{};
    Cpuid(r, 7, 0);
    return (r[reg_index] & (1 << bit)) != 0;
}

json CpuIdentity() {
    int r[4]{};
    Cpuid(r, 0);
    int max_leaf = r[0];
    char vendor[13]{};
    std::memcpy(vendor + 0, &r[1], 4); // EBX
    std::memcpy(vendor + 4, &r[3], 4); // EDX
    std::memcpy(vendor + 8, &r[2], 4); // ECX

    int ext[4]{};
    Cpuid(ext, 0x80000000);
    unsigned int max_ext = static_cast<unsigned int>(ext[0]);
    char brand[49]{};
    if (max_ext >= 0x80000004) {
        for (unsigned int leaf = 0; leaf < 3; ++leaf) {
            int br[4]{};
            Cpuid(br, static_cast<int>(0x80000002 + leaf));
            std::memcpy(brand + leaf * 16, br, 16);
        }
    }

    std::string brand_s = brand;
    brand_s.erase(brand_s.begin(), std::find_if(brand_s.begin(), brand_s.end(),
        [](unsigned char ch) { return !std::isspace(ch); }));
    if (brand_s.empty()) {
        brand_s = RegString(HKEY_LOCAL_MACHINE,
            L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",
            L"ProcessorNameString");
    }

    int f1[4]{};
    Cpuid(f1, 1);
    int stepping = f1[0] & 0xF;
    int model = (f1[0] >> 4) & 0xF;
    int family = (f1[0] >> 8) & 0xF;
    int ext_model = (f1[0] >> 16) & 0xF;
    int ext_family = (f1[0] >> 20) & 0xFF;
    int display_family = family == 0xF ? family + ext_family : family;
    int display_model = (family == 0x6 || family == 0xF) ? (ext_model << 4) + model : model;

    SYSTEM_INFO si{};
    GetNativeSystemInfo(&si);

    DWORD len = 0;
    GetLogicalProcessorInformationEx(RelationAll, nullptr, &len);
    std::vector<BYTE> topo(len);
    int packages = 0;
    int physical_cores = 0;
    int numa_nodes = 0;
    json caches = json::array();
    if (len > 0 &&
        GetLogicalProcessorInformationEx(RelationAll,
            reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(topo.data()), &len)) {
        BYTE* ptr = topo.data();
        BYTE* end = topo.data() + len;
        while (ptr < end) {
            auto* info = reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(ptr);
            if (info->Relationship == RelationProcessorPackage) ++packages;
            else if (info->Relationship == RelationProcessorCore) ++physical_cores;
            else if (info->Relationship == RelationNumaNode) ++numa_nodes;
            else if (info->Relationship == RelationCache) {
                const auto& c = info->Cache;
                caches.push_back({
                    {"level", c.Level},
                    {"size_bytes", c.CacheSize},
                    {"line_size", c.LineSize},
                    {"associativity", c.Associativity}
                });
            }
            ptr += info->Size;
        }
    }

    json features;
    features["sse"] = (f1[3] & (1 << 25)) != 0;
    features["sse2"] = (f1[3] & (1 << 26)) != 0;
    features["sse3"] = (f1[2] & (1 << 0)) != 0;
    features["ssse3"] = (f1[2] & (1 << 9)) != 0;
    features["sse4_1"] = (f1[2] & (1 << 19)) != 0;
    features["sse4_2"] = (f1[2] & (1 << 20)) != 0;
    features["aes"] = (f1[2] & (1 << 25)) != 0;
    features["fma"] = (f1[2] & (1 << 12)) != 0;
    features["avx"] = (f1[2] & (1 << 28)) != 0;
    features["avx2"] = max_leaf >= 7 && CpuFeatureLeaf7(5, 1); // EBX bit 5
    features["avx512f"] = max_leaf >= 7 && CpuFeatureLeaf7(16, 1); // EBX bit 16
    features["sha"] = max_leaf >= 7 && CpuFeatureLeaf7(29, 1); // EBX bit 29
    features["amx_tile"] = max_leaf >= 7 && CpuFeatureLeaf7(24, 3); // EDX bit 24
    features["amx_int8"] = max_leaf >= 7 && CpuFeatureLeaf7(25, 3); // EDX bit 25
    features["amx_bf16"] = max_leaf >= 7 && CpuFeatureLeaf7(22, 3); // EDX bit 22

    json cpu;
    cpu["component_id"] = "cpu0";
    cpu["class"] = "cpu";
    cpu["vendor"] = vendor;
    cpu["name"] = brand_s;
    cpu["family"] = display_family;
    cpu["model"] = display_model;
    cpu["stepping"] = stepping;
    cpu["logical_processors"] = si.dwNumberOfProcessors;
    if (physical_cores > 0) cpu["physical_cores"] = physical_cores;
    else cpu["physical_cores"] = nullptr;
    cpu["packages"] = packages > 0 ? packages : 1;
    cpu["numa_nodes"] = numa_nodes;
    cpu["cache_topology"] = caches;
    cpu["instruction_sets"] = features;
    cpu["capabilities"] = {
        {"usage", Capability(true, "measured", "valid", "NtQuerySystemInformation")},
        {"frequency", Capability(true, "derived", "valid", "PDH Processor Information + registry rated MHz")},
        {"cache_topology", Capability(!caches.empty(), caches.empty() ? "unavailable" : "measured",
            caches.empty() ? "unsupported" : "valid", "GetLogicalProcessorInformationEx",
            caches.empty() ? "Windows topology did not return cache records" : "")},
        {"temperature", Capability(false, "unavailable", "unsupported",
            "Generic ACPI thermal zones only",
            "CPU package/core temperature provider is not implemented in this release")},
        {"package_power", Capability(false, "unavailable", "unsupported",
            "CPU package provider",
            "CPU package power is not implemented on Windows in this release")}
    };
    return cpu;
}

json DxgiGpuInventory(const ShmBlock* shm) {
    json arr = json::array();
    IDXGIFactory1* factory = nullptr;
    if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1),
            reinterpret_cast<void**>(&factory)))) {
        return arr;
    }

    for (UINT i = 0; ; ++i) {
        IDXGIAdapter1* adapter = nullptr;
        HRESULT hr = factory->EnumAdapters1(i, &adapter);
        if (hr == DXGI_ERROR_NOT_FOUND) break;
        if (FAILED(hr) || !adapter) continue;

        DXGI_ADAPTER_DESC1 desc{};
        adapter->GetDesc1(&desc);
        adapter->Release();

        const std::string vendor = VendorFromPciId(desc.VendorId);
        std::string name = WideToUtf8(desc.Description);
        bool software = (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0;

        json gpu;
        gpu["component_id"] = "gpu" + std::to_string(i);
        gpu["class"] = software ? "software_adapter" : "gpu";
        gpu["name"] = name;
        gpu["vendor"] = vendor;
        gpu["vendor_id"] = Hex4(desc.VendorId);
        gpu["device_id"] = Hex4(desc.DeviceId);
        gpu["subsys_id"] = Hex4(desc.SubSysId);
        gpu["revision"] = desc.Revision;
        gpu["dxgi_adapter_index"] = i;
        gpu["dedicated_video_memory_mb"] =
            static_cast<double>(desc.DedicatedVideoMemory) / (1024.0 * 1024.0);
        gpu["shared_system_memory_mb"] =
            static_cast<double>(desc.SharedSystemMemory) / (1024.0 * 1024.0);
        gpu["provider_preference"] = GpuProviderForVendor(vendor);
        gpu["identity_confidence"] = "medium";
        gpu["identity_basis"] = "DXGI adapter order plus vendor/device ID; provider UUID/PCI identity should be added for stronger cross-boot correlation";

        double temp = 0.0;
        double watts = 0.0;
        double cuda_major = 0.0;
        double cuda_minor = 0.0;
        double tensor_gen = 0.0;
        bool tensor = false;
        bool sensor_match = false;
        if (shm && i < shm->hdr.active_gpu_count && i < MetricId::GPU_MAX_COUNT) {
            temp = shm->metrics[gpu_metric(i, GpuOff::TEMP_C)].current;
            watts = shm->metrics[gpu_metric(i, GpuOff::POWER_W)].current;
            cuda_major = shm->metrics[gpu_metric(i, GpuOff::CUDA_CC_MAJOR)].current;
            cuda_minor = shm->metrics[gpu_metric(i, GpuOff::CUDA_CC_MINOR)].current;
            tensor_gen = shm->metrics[gpu_metric(i, GpuOff::TENSOR_CORE_GEN)].current;
            tensor = shm->metrics[gpu_metric(i, GpuOff::TENSOR_ACTIVE)].current > 0.5;
            sensor_match = true;
            std::string shm_name = shm->hdr.gpu_name[i];
            if (!shm_name.empty()) gpu["sensor_name"] = shm_name;
        }

        const bool is_nvidia = vendor == "NVIDIA";
        const bool is_amd = vendor == "AMD";
        const bool is_intel = vendor == "Intel";
        gpu["capabilities"] = {
            {"utilization", Capability(sensor_match, sensor_match ? "measured" : "unavailable",
                sensor_match ? "valid" : "unsupported", sensor_match ? "Vendor API or PDH" : "No matching active sensor row")},
            {"temperature", Capability(temp > 0.0, QualityFromValue(temp).c_str(),
                temp > 0.0 ? "valid" : "unsupported", is_nvidia ? "NVML" : is_intel ? "IGCL" : is_amd ? "ADL current path" : "Vendor API",
                temp > 0.0 ? "" : "Provider did not report a positive temperature")},
            {"power_watts", Capability(watts > 0.0, QualityFromValue(watts).c_str(),
                watts > 0.0 ? "valid" : "unsupported", is_nvidia ? "NVML" : is_intel ? "IGCL" : is_amd ? "ADL current path" : "Vendor API",
                watts > 0.0 ? "" : "Provider did not report positive GPU watts")},
            {"energy_counter", Capability(false, "unavailable", "not_implemented",
                is_nvidia ? "NVML/DCGM candidate" : is_amd ? "AMD SMI/ROCm SMI candidate" : is_intel ? "Level Zero Sysman candidate" : "Vendor provider candidate",
                "Energy counter discovery is planned but not implemented in this release")},
            {"nvidia_cuda_compute_capability", Capability(is_nvidia && cuda_major > 0.0,
                is_nvidia && cuda_major > 0.0 ? "measured" : "unavailable",
                is_nvidia && cuda_major > 0.0 ? "valid" : "unsupported", "NVML",
                is_nvidia ? "NVML did not report CUDA compute capability" : "Not an NVIDIA CUDA device")},
            {"nvidia_tensor_cores", Capability(is_nvidia && tensor,
                is_nvidia && tensor ? "inferred" : "unavailable",
                is_nvidia && tensor ? "valid" : "unsupported", "CUDA compute capability inference",
                is_nvidia ? "Tensor-core presence is inferred from compute capability, not directly measured" : "Not an NVIDIA CUDA device")},
            {"amd_matrix_acceleration", Capability(false, "unavailable", "not_implemented",
                "AMD SMI/ADLX/static architecture mapping candidate",
                is_amd ? "AMD matrix/CDNA/RDNA capability mapping is planned" : "Not an AMD GPU")},
            {"intel_xmx_matrix_engines", Capability(false, "unavailable", "not_implemented",
                "Level Zero Sysman/static architecture mapping candidate",
                is_intel ? "Intel XMX/DPAS capability mapping is planned" : "Not an Intel GPU")}
        };
        if (cuda_major > 0.0) {
            gpu["nvidia_cuda_compute_capability"] = {
                {"major", cuda_major},
                {"minor", cuda_minor}
            };
            gpu["nvidia_tensor_core_generation"] = tensor_gen;
        }
        arr.push_back(gpu);
    }

    factory->Release();
    return arr;
}

} // namespace

json BuildHardwareInventoryJson(const ShmBlock* shm) {
    json root;
    root["schema"] = "telemetryapp.hardware_inventory.v1";
    root["truth_model"] =
        "identity first, provider availability second, metric capability third; unsupported is never encoded as a valid zero";
    root["generated_at_ms"] = []() -> int64_t {
        FILETIME ft{};
        GetSystemTimeAsFileTime(&ft);
        ULARGE_INTEGER u{ ft.dwLowDateTime, ft.dwHighDateTime };
        return static_cast<int64_t>((u.QuadPart - 116444736000000000ULL) / 10000ULL);
    }();
    root["cpus"] = json::array({CpuIdentity()});
    root["gpus"] = DxgiGpuInventory(shm);
    root["providers"] = {
        {"cpu", {"CPUID", "GetLogicalProcessorInformationEx", "PDH", "NtQuerySystemInformation"}},
        {"gpu", {"DXGI", "NVML when present", "ADL current AMD path", "IGCL current Intel path", "PDH fallback"}},
        {"planned", {"AMD SMI/ADLX", "Intel Level Zero Sysman", "NVML/DCGM energy counters", "BMC/Redfish", "PDU/UPS/external meter"}}
    };
    root["release_limits"] = {
        "CPU package/core temperatures require a future provider beyond generic Windows ACPI zones.",
        "CPU package power is unavailable in the current Windows implementation.",
        "GPU energy counters are planned; current GPU watts remain source-qualified.",
        "AMD and Intel matrix/AI accelerator feature mapping is planned and must not be labeled as NVIDIA tensor cores."
    };
    return root;
}

} // namespace Service
