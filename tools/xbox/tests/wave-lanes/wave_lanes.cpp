// SPDX-FileCopyrightText: Copyright 2026 Bonsai contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Runs lanes.comp (the warp compaction of Pokopia's terrain compute shaders) through the chain the
// console uses (SPIR-V -> Bonsai's spirv_to_dxil.dll with Bonsai's configuration -> dxil.dll
// signature) and executes it on WARP, the software D3D12 device, then checks every invocation:
//
//   - the lt mask it saw is the guest's, (1 << (lane & 31)) - 1, for lanes below and past 32;
//   - the compaction is exact: every invocation with an item is written once, nothing else.
//
// Usage: wave_lanes <out_dir> <variant>=<spirv> ...   (variant: builtin | guest)
// Exit code: 1 if the harness itself fails or the guest variant is wrong; a wrong builtin variant
// is reported (it is what the guest variant replaces), not a failure.
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <dxcapi.h>
#include <wrl/client.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

#include "spirv_to_dxil.h"

using Microsoft::WRL::ComPtr;

namespace {

constexpr uint32_t GROUP_SIZE = 64;   // lanes.comp local_size_x
constexpr uint32_t GROUPS = 64;
constexpr uint32_t THREADS = GROUP_SIZE * GROUPS;
// Bonsai's register spaces (d3d12_shader_compiler.h).
constexpr uint32_t PUSH_CONSTANT_SPACE = 30;
constexpr uint32_t RUNTIME_DATA_SPACE = 31;

void Check(HRESULT hr, const char* what) {
    if (FAILED(hr)) {
        char text[256];
        std::snprintf(text, sizeof(text), "%s failed: 0x%08lx", what, static_cast<unsigned long>(hr));
        throw std::runtime_error(text);
    }
}

std::vector<uint8_t> ReadFile(const std::filesystem::path& path) {
    std::ifstream file{path, std::ios::binary};
    if (!file) throw std::runtime_error("cannot read " + path.string());
    return {std::istreambuf_iterator<char>(file), {}};
}

void WriteFile(const std::filesystem::path& path, const void* data, size_t size) {
    std::ofstream file{path, std::ios::binary};
    file.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
    if (!file) throw std::runtime_error("cannot write " + path.string());
}

bool HasItem(uint32_t gid) {
    return (((gid * 2654435761u) >> 28) & 1u) != 0u;
}

// A blob over bytes the caller owns, for IDxcValidator.
class Blob final : public IDxcBlob {
public:
    Blob(void* data, size_t size) : data_{data}, size_{size} {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** out) override {
        if (riid == __uuidof(IDxcBlob) || riid == __uuidof(IUnknown)) {
            *out = static_cast<IDxcBlob*>(this);
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return 2; }
    ULONG STDMETHODCALLTYPE Release() override { return 1; }
    LPVOID STDMETHODCALLTYPE GetBufferPointer() override { return data_; }
    SIZE_T STDMETHODCALLTYPE GetBufferSize() override { return size_; }

private:
    void* data_;
    size_t size_;
};

struct Translator {
    decltype(&spirv_to_dxil) translate{};
    decltype(&spirv_to_dxil_free) free_dxil{};
    ComPtr<IDxcValidator> validator;

    Translator() {
        HMODULE spirv = LoadLibraryW(L"spirv_to_dxil.dll");
        if (!spirv) {
            // 126: a DLL it imports is missing (the app CRT); 193: wrong architecture.
            throw std::runtime_error("spirv_to_dxil.dll did not load, Windows error " +
                                     std::to_string(GetLastError()));
        }
        translate = reinterpret_cast<decltype(translate)>(GetProcAddress(spirv, "spirv_to_dxil"));
        free_dxil = reinterpret_cast<decltype(free_dxil)>(GetProcAddress(spirv, "spirv_to_dxil_free"));
        if (!translate || !free_dxil) throw std::runtime_error("spirv_to_dxil.dll lacks its entry points");
        HMODULE dxil = LoadLibraryW(L"dxil.dll");
        if (!dxil) throw std::runtime_error("dxil.dll not found");
        const auto create = reinterpret_cast<DxcCreateInstanceProc>(GetProcAddress(dxil, "DxcCreateInstance"));
        if (!create) throw std::runtime_error("dxil.dll lacks DxcCreateInstance");
        Check(create(CLSID_DxcValidator, IID_PPV_ARGS(&validator)), "DxcCreateInstance(validator)");
    }

    // SPIR-V -> signed DXIL with Bonsai's configuration (ShaderCompiler::MakeConf).
    std::vector<uint8_t> Compile(const std::vector<uint8_t>& spirv, bool dump_nir) const {
        dxil_spirv_runtime_conf conf{};
        conf.runtime_data_cbv = {.register_space = RUNTIME_DATA_SPACE, .base_shader_register = 0};
        conf.push_constant_cbv = {.register_space = PUSH_CONSTANT_SPACE, .base_shader_register = 0};
        conf.first_vertex_and_base_instance_mode = DXIL_SPIRV_SYSVAL_TYPE_ZERO;
        conf.workgroup_id_mode = DXIL_SPIRV_SYSVAL_TYPE_NATIVE;
        conf.declared_read_only_images_as_srvs = true;
        conf.shader_model_max = SHADER_MODEL_6_4;
        dxil_spirv_debug_options debug{.dump_nir = dump_nir};
        dxil_spirv_logger logger{nullptr, [](void*, const char* msg) { std::printf("  spirv_to_dxil: %s\n", msg); }};
        dxil_spirv_object object{};
        if (!translate(reinterpret_cast<const uint32_t*>(spirv.data()), spirv.size() / 4, nullptr, 0,
                       DXIL_SPIRV_SHADER_COMPUTE, "main", DXIL_VALIDATOR_1_4, &debug, &conf,
                       &logger, &object)) {
            throw std::runtime_error("spirv_to_dxil refused the shader");
        }
        std::vector<uint8_t> dxil(static_cast<uint8_t*>(object.binary.buffer),
                                  static_cast<uint8_t*>(object.binary.buffer) + object.binary.size);
        free_dxil(&object);
        Blob blob{dxil.data(), dxil.size()};
        ComPtr<IDxcOperationResult> result;
        Check(validator->Validate(&blob, DxcValidatorFlags_InPlaceEdit, &result), "IDxcValidator::Validate");
        HRESULT status = E_FAIL;
        result->GetStatus(&status);
        if (FAILED(status)) {
            ComPtr<IDxcBlobEncoding> errors;
            result->GetErrorBuffer(&errors);
            std::string text = errors ? std::string(static_cast<const char*>(errors->GetBufferPointer()),
                                                    errors->GetBufferSize())
                                      : std::string{"(no message)"};
            throw std::runtime_error("DXIL validation failed: " + text);
        }
        return dxil;
    }
};

struct Device {
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12CommandQueue> queue;
    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> list;
    ComPtr<ID3D12Fence> fence;
    uint64_t fence_value{};
    ComPtr<ID3D12RootSignature> root;

    Device() {
        ComPtr<IDXGIFactory4> factory;
        Check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)), "CreateDXGIFactory1");
        ComPtr<IDXGIAdapter> warp;
        Check(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp)), "EnumWarpAdapter");
        Check(D3D12CreateDevice(warp.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)), "D3D12CreateDevice(WARP)");
        D3D12_COMMAND_QUEUE_DESC queue_desc{.Type = D3D12_COMMAND_LIST_TYPE_DIRECT};
        Check(device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&queue)), "CreateCommandQueue");
        Check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator)), "CreateCommandAllocator");
        Check(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr, IID_PPV_ARGS(&list)), "CreateCommandList");
        Check(list->Close(), "Close");
        Check(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)), "CreateFence");

        // u0, u1 in space 0 (the shader's set 0 bindings 0 and 1), plus Bonsai's runtime data and
        // push constant buffers in case the translation declares them.
        D3D12_DESCRIPTOR_RANGE range{.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV, .NumDescriptors = 2,
                                     .BaseShaderRegister = 0, .RegisterSpace = 0,
                                     .OffsetInDescriptorsFromTableStart = 0};
        D3D12_ROOT_PARAMETER params[3]{};
        params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[0].DescriptorTable = {1, &range};
        params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        params[1].Descriptor = {0, RUNTIME_DATA_SPACE};
        params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        params[2].Descriptor = {0, PUSH_CONSTANT_SPACE};
        D3D12_ROOT_SIGNATURE_DESC desc{.NumParameters = 3, .pParameters = params};
        ComPtr<ID3DBlob> blob, error;
        if (FAILED(D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &error))) {
            throw std::runtime_error(std::string("root signature: ") +
                                     (error ? static_cast<const char*>(error->GetBufferPointer()) : "?"));
        }
        Check(device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&root)),
              "CreateRootSignature");
    }

    ComPtr<ID3D12Resource> Buffer(uint64_t size, D3D12_HEAP_TYPE heap, D3D12_RESOURCE_STATES state,
                                  D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE) {
        D3D12_HEAP_PROPERTIES props{.Type = heap};
        D3D12_RESOURCE_DESC desc{.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER, .Width = size, .Height = 1,
                                 .DepthOrArraySize = 1, .MipLevels = 1, .SampleDesc = {1, 0},
                                 .Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR, .Flags = flags};
        ComPtr<ID3D12Resource> resource;
        Check(device->CreateCommittedResource(&props, D3D12_HEAP_FLAG_NONE, &desc, state, nullptr,
                                              IID_PPV_ARGS(&resource)), "CreateCommittedResource");
        return resource;
    }

    void Submit() {
        Check(list->Close(), "Close");
        ID3D12CommandList* lists[]{list.Get()};
        queue->ExecuteCommandLists(1, lists);
        Check(queue->Signal(fence.Get(), ++fence_value), "Signal");
        HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        Check(fence->SetEventOnCompletion(fence_value, event), "SetEventOnCompletion");
        WaitForSingleObject(event, INFINITE);
        CloseHandle(event);
        Check(device->GetDeviceRemovedReason(), "device removed");
    }
};

struct Run {
    std::vector<uint32_t> items;     // counter, then the items
    std::vector<uint32_t> debug;     // 4 words per invocation
};

Run Execute(Device& gpu, const std::vector<uint8_t>& dxil) {
    D3D12_COMPUTE_PIPELINE_STATE_DESC pso_desc{};
    pso_desc.pRootSignature = gpu.root.Get();
    pso_desc.CS = {dxil.data(), dxil.size()};
    ComPtr<ID3D12PipelineState> pso;
    Check(gpu.device->CreateComputePipelineState(&pso_desc, IID_PPV_ARGS(&pso)), "CreateComputePipelineState");

    const uint64_t items_size = 4ull * (1 + THREADS);
    const uint64_t debug_size = 16ull * THREADS;
    auto items = gpu.Buffer(items_size, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COPY_DEST,
                            D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    auto debug = gpu.Buffer(debug_size, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COPY_DEST,
                            D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    auto upload = gpu.Buffer(items_size + debug_size, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
    auto readback = gpu.Buffer(items_size + debug_size, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
    auto constants = gpu.Buffer(256, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
    {
        void* mapped{};
        Check(upload->Map(0, nullptr, &mapped), "Map(upload)");
        // Items start as 0xFFFFFFFF, so an unwritten slot is told from item 0.
        std::memset(mapped, 0xff, static_cast<size_t>(items_size + debug_size));
        static_cast<uint32_t*>(mapped)[0] = 0;  // counter
        upload->Unmap(0, nullptr);
        Check(constants->Map(0, nullptr, &mapped), "Map(constants)");
        const uint32_t runtime[4]{GROUPS, 1, 1, 0};  // dxil_spirv_compute_runtime_data
        std::memset(mapped, 0, 256);
        std::memcpy(mapped, runtime, sizeof(runtime));
        constants->Unmap(0, nullptr);
    }

    D3D12_DESCRIPTOR_HEAP_DESC heap_desc{.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, .NumDescriptors = 2,
                                         .Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE};
    ComPtr<ID3D12DescriptorHeap> heap;
    Check(gpu.device->CreateDescriptorHeap(&heap_desc, IID_PPV_ARGS(&heap)), "CreateDescriptorHeap");
    const UINT increment = gpu.device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    auto cpu = heap->GetCPUDescriptorHandleForHeapStart();
    for (ID3D12Resource* resource : {items.Get(), debug.Get()}) {
        D3D12_UNORDERED_ACCESS_VIEW_DESC uav{.Format = DXGI_FORMAT_R32_TYPELESS,
                                             .ViewDimension = D3D12_UAV_DIMENSION_BUFFER};
        uav.Buffer.NumElements = static_cast<UINT>(resource->GetDesc().Width / 4);
        uav.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
        gpu.device->CreateUnorderedAccessView(resource, nullptr, &uav, cpu);
        cpu.ptr += increment;
    }

    Check(gpu.allocator->Reset(), "Reset(allocator)");
    Check(gpu.list->Reset(gpu.allocator.Get(), pso.Get()), "Reset(list)");
    gpu.list->CopyBufferRegion(items.Get(), 0, upload.Get(), 0, items_size);
    gpu.list->CopyBufferRegion(debug.Get(), 0, upload.Get(), items_size, debug_size);
    D3D12_RESOURCE_BARRIER to_uav[2]{};
    for (int i = 0; i < 2; ++i) {
        to_uav[i].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        to_uav[i].Transition = {i ? debug.Get() : items.Get(), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
                                D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS};
    }
    gpu.list->ResourceBarrier(2, to_uav);
    ID3D12DescriptorHeap* heaps[]{heap.Get()};
    gpu.list->SetDescriptorHeaps(1, heaps);
    gpu.list->SetComputeRootSignature(gpu.root.Get());
    gpu.list->SetComputeRootDescriptorTable(0, heap->GetGPUDescriptorHandleForHeapStart());
    gpu.list->SetComputeRootConstantBufferView(1, constants->GetGPUVirtualAddress());
    gpu.list->SetComputeRootConstantBufferView(2, constants->GetGPUVirtualAddress());
    gpu.list->Dispatch(GROUPS, 1, 1);
    D3D12_RESOURCE_BARRIER to_copy[2]{};
    for (int i = 0; i < 2; ++i) {
        to_copy[i].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        to_copy[i].Transition = {i ? debug.Get() : items.Get(), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
                                 D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE};
    }
    gpu.list->ResourceBarrier(2, to_copy);
    gpu.list->CopyBufferRegion(readback.Get(), 0, items.Get(), 0, items_size);
    gpu.list->CopyBufferRegion(readback.Get(), items_size, debug.Get(), 0, debug_size);
    gpu.Submit();

    Run run;
    void* mapped{};
    Check(readback->Map(0, nullptr, &mapped), "Map(readback)");
    const auto* words = static_cast<const uint32_t*>(mapped);
    run.items.assign(words, words + items_size / 4);
    run.debug.assign(words + items_size / 4, words + (items_size + debug_size) / 4);
    readback->Unmap(0, nullptr);
    return run;
}

struct Verdict {
    uint32_t wave = 0;
    uint32_t lt_wrong_low = 0, lt_wrong_high = 0, lanes_low = 0, lanes_high = 0;
    uint32_t missing = 0, duplicated = 0, stray = 0, expected = 0, counter = 0;
    bool Ok() const {
        return lt_wrong_low == 0 && lt_wrong_high == 0 && missing == 0 && duplicated == 0 && stray == 0 &&
               counter == expected;
    }
};

Verdict Judge(const Run& run, const char* name) {
    Verdict v;
    std::map<uint32_t, uint32_t> examples;  // lane -> lt seen, first wrong ones
    for (uint32_t gid = 0; gid < THREADS; ++gid) {
        const uint32_t lane = run.debug[gid * 4] & 0xffff;
        const uint32_t size = run.debug[gid * 4] >> 16;
        const uint32_t lt = run.debug[gid * 4 + 1];
        v.wave = std::max(v.wave, size);
        const uint32_t want = (1u << (lane & 31u)) - 1u;
        uint32_t& wrong = lane < 32 ? v.lt_wrong_low : v.lt_wrong_high;
        (lane < 32 ? v.lanes_low : v.lanes_high)++;
        if (lt != want) {
            ++wrong;
            if (examples.size() < 6 && !examples.count(lane)) examples[lane] = lt;
        }
    }
    v.counter = run.items[0];
    std::vector<uint32_t> seen(THREADS, 0);
    for (uint32_t gid = 0; gid < THREADS; ++gid) v.expected += HasItem(gid) ? 1u : 0u;
    for (uint32_t index = 0; index < THREADS; ++index) {
        const uint32_t item = run.items[1 + index];
        if (item == 0xffffffffu) continue;
        if (item >= THREADS || !HasItem(item)) { ++v.stray; continue; }
        ++seen[item];
    }
    for (uint32_t gid = 0; gid < THREADS; ++gid) {
        if (!HasItem(gid)) continue;
        if (seen[gid] == 0) ++v.missing;
        if (seen[gid] > 1) v.duplicated += seen[gid] - 1;
    }
    std::printf("[%s] subgroup size %u | lt mask wrong: %u of %u lanes 0-31, %u of %u lanes 32+ | "
                "items: counter %u, expected %u, missing %u, duplicated %u, stray %u => %s\n",
                name, v.wave, v.lt_wrong_low, v.lanes_low, v.lt_wrong_high, v.lanes_high, v.counter,
                v.expected, v.missing, v.duplicated, v.stray, v.Ok() ? "OK" : "WRONG");
    for (const auto& [lane, lt] : examples) {
        std::printf("[%s]   lane %u saw lt %08x, guest wants %08x\n", name, lane, lt, (1u << (lane & 31u)) - 1u);
    }
    return v;
}

} // namespace

int main(int argc, char** argv) try {
    if (argc < 3) throw std::runtime_error("usage: wave_lanes <out_dir> <variant>=<spirv> ...");
    const std::filesystem::path out{argv[1]};
    std::filesystem::create_directories(out);
    Translator translator;
    Device gpu;
    {
        D3D12_FEATURE_DATA_D3D12_OPTIONS1 o1{};
        gpu.device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS1, &o1, sizeof(o1));
        D3D12_FEATURE_DATA_SHADER_MODEL sm{D3D_SHADER_MODEL_6_6};
        gpu.device->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &sm, sizeof(sm));
        std::printf("WARP: wave ops %s, lanes %u-%u, highest shader model %x\n", o1.WaveOps ? "yes" : "no",
                    o1.WaveLaneCountMin, o1.WaveLaneCountMax, static_cast<unsigned>(sm.HighestShaderModel));
    }
    bool guest_ok = true, ran_guest = false;
    for (int i = 2; i < argc; ++i) {
        const std::string arg = argv[i];
        const auto eq = arg.find('=');
        if (eq == std::string::npos) throw std::runtime_error("expected <variant>=<spirv>: " + arg);
        const std::string name = arg.substr(0, eq);
        const auto spirv = ReadFile(arg.substr(eq + 1));
        std::printf("== %s: translating %zu bytes of SPIR-V (NIR follows)\n", name.c_str(), spirv.size());
        std::fflush(stdout);
        const auto dxil = translator.Compile(spirv, true);
        WriteFile(out / (name + ".dxil"), dxil.data(), dxil.size());
        const Verdict verdict = Judge(Execute(gpu, dxil), name.c_str());
        if (name == "guest") {
            ran_guest = true;
            guest_ok = verdict.Ok();
        }
        if (name == "builtin" && !verdict.Ok()) {
            std::printf("[builtin] CONFIRMED: the built-in lt mask path is wrong on this device "
                        "(what PR #15 replaces).\n");
        }
        if (verdict.wave < 64) {
            std::printf("[%s] note: this device ran %u-lane subgroups; lanes 32+ (the console's upper half) "
                        "were not exercised here.\n", name.c_str(), verdict.wave);
        }
    }
    if (ran_guest && !guest_ok) {
        std::printf("FAIL: the guest-mask compaction is wrong.\n");
        return 1;
    }
    std::printf("done\n");
    return 0;
} catch (const std::exception& error) {
    std::printf("ERROR: %s\n", error.what());
    return 1;
}
