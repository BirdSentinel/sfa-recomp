// Execute the actual translator's depth-only variants on D3D12 and read back
// depth. This catches invalid DXBC, faceness, depth scaling, and conversion bugs.
#include <windows.h>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>
#include <rex/graphics/pipeline/shader/dxbc_translator.h>

using Microsoft::WRL::ComPtr;
using Translator = rex::graphics::DxbcShaderTranslator;
using Mode = Translator::Modification::DepthStencilMode;
constexpr UINT kSize = 32;

void Check(HRESULT result, const char* operation) {
  if (FAILED(result)) {
    char message[200];
    std::snprintf(message, sizeof(message), "%s failed: %08lX", operation, result);
    throw std::runtime_error(message);
  }
}

ComPtr<ID3DBlob> Compile(const char* source, const char* profile) {
  ComPtr<ID3DBlob> blob, errors;
  HRESULT hr = D3DCompile(source, std::strlen(source), nullptr, nullptr, nullptr,
                         "main", profile, D3DCOMPILE_OPTIMIZATION_LEVEL3, 0,
                         &blob, &errors);
  if (errors) std::fputs(static_cast<const char*>(errors->GetBufferPointer()), stderr);
  Check(hr, "D3DCompile");
  return blob;
}

struct Gpu {
  ComPtr<ID3D12Device> device;
  ComPtr<ID3D12CommandQueue> queue;
  ComPtr<ID3D12CommandAllocator> allocator;
  ComPtr<ID3D12GraphicsCommandList> commands;
  ComPtr<ID3D12Fence> fence;
  ComPtr<ID3D12RootSignature> root;
  ComPtr<ID3D12DescriptorHeap> dsv_heap;
  ComPtr<ID3D12Resource> depth, readback, constants;
  D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
  HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  UINT64 fence_value = 0;
  void* mapped_constants = nullptr;

  ComPtr<ID3D12Resource> Buffer(UINT64 size, D3D12_HEAP_TYPE type,
                              D3D12_RESOURCE_STATES state) {
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = type;
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = size;
    desc.Height = desc.DepthOrArraySize = desc.MipLevels = 1;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ComPtr<ID3D12Resource> resource;
    Check(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, state,
                                         nullptr, IID_PPV_ARGS(&resource)), "buffer");
    return resource;
  }

  Gpu() {
    // A headless test: no game process or window is required.
    Check(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)), "device");
    D3D12_COMMAND_QUEUE_DESC q{};
    Check(device->CreateCommandQueue(&q, IID_PPV_ARGS(&queue)), "queue");
    Check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                        IID_PPV_ARGS(&allocator)), "allocator");
    Check(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(),
                                   nullptr, IID_PPV_ARGS(&commands)), "commands");
    Check(commands->Close(), "close initial commands");
    Check(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)), "fence");
    D3D12_ROOT_PARAMETER parameter{};
    parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    parameter.Descriptor.ShaderRegister = 0;
    parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    D3D12_ROOT_SIGNATURE_DESC desc{};
    desc.NumParameters = 1;
    desc.pParameters = &parameter;
    ComPtr<ID3DBlob> serialized, errors;
    Check(D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1,
                                     &serialized, &errors), "root serialization");
    Check(device->CreateRootSignature(0, serialized->GetBufferPointer(), serialized->GetBufferSize(),
                                     IID_PPV_ARGS(&root)), "root signature");
    D3D12_DESCRIPTOR_HEAP_DESC heap_desc{};
    heap_desc.NumDescriptors = 1;
    heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    Check(device->CreateDescriptorHeap(&heap_desc, IID_PPV_ARGS(&dsv_heap)), "DSV heap");
    D3D12_RESOURCE_DESC texture{};
    texture.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texture.Width = texture.Height = kSize;
    texture.DepthOrArraySize = texture.MipLevels = texture.SampleDesc.Count = 1;
    texture.Format = DXGI_FORMAT_D32_FLOAT;
    texture.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_CLEAR_VALUE clear{};
    clear.Format = texture.Format;
    clear.DepthStencil.Depth = 1.0f;
    Check(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &texture,
                                         D3D12_RESOURCE_STATE_DEPTH_WRITE, &clear,
                                         IID_PPV_ARGS(&depth)), "depth texture");
    device->CreateDepthStencilView(depth.Get(), nullptr, dsv_heap->GetCPUDescriptorHandleForHeapStart());
    UINT64 bytes;
    device->GetCopyableFootprints(&texture, 0, 1, 0, &footprint, nullptr, nullptr, &bytes);
    readback = Buffer(bytes, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
    constants = Buffer(1024, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
    Check(constants->Map(0, nullptr, &mapped_constants), "map constants");
  }
  ~Gpu() { if (event) CloseHandle(event); }

  std::vector<float> Draw(ID3D12PipelineState* pipeline, float z, float dx, float dy,
                          float scale, float offset) {
    Translator::SystemConstants cb{};
    // The test VS uses c0.yzw for its plane equation in guest [0,1] depth.
    cb.tessellation_factor_range_min = z;
    cb.tessellation_factor_range_max = dx;
    std::memcpy(&cb.line_loop_closing_index, &dy, sizeof(dy));
    cb.edram_poly_offset_front_scale = scale;
    cb.edram_poly_offset_front_offset = offset;
    cb.edram_poly_offset_back_scale = scale * -0.5f;
    cb.edram_poly_offset_back_offset = offset * -0.5f;
    std::memcpy(mapped_constants, &cb, sizeof(cb));
    Check(allocator->Reset(), "reset allocator");
    Check(commands->Reset(allocator.Get(), pipeline), "reset commands");
    auto dsv = dsv_heap->GetCPUDescriptorHandleForHeapStart();
    commands->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
    commands->OMSetRenderTargets(0, nullptr, FALSE, &dsv);
    commands->SetGraphicsRootSignature(root.Get());
    commands->SetGraphicsRootConstantBufferView(0, constants->GetGPUVirtualAddress());
    D3D12_VIEWPORT viewport{0, 0, float(kSize), float(kSize), 0, 0.5f};
    D3D12_RECT rect{0, 0, kSize, kSize};
    commands->RSSetViewports(1, &viewport);
    commands->RSSetScissorRects(1, &rect);
    commands->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    commands->DrawInstanced(3, 1, 0, 0);
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = depth.Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_DEPTH_WRITE;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    commands->ResourceBarrier(1, &barrier);
    D3D12_TEXTURE_COPY_LOCATION source{}, destination{};
    source.pResource = depth.Get();
    source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    destination.pResource = readback.Get();
    destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    destination.PlacedFootprint = footprint;
    commands->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
    std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
    commands->ResourceBarrier(1, &barrier);
    Check(commands->Close(), "close commands");
    ID3D12CommandList* lists[]{commands.Get()};
    queue->ExecuteCommandLists(1, lists);
    Check(queue->Signal(fence.Get(), ++fence_value), "signal");
    Check(fence->SetEventOnCompletion(fence_value, event), "fence wait");
    if (WaitForSingleObject(event, 15000) != WAIT_OBJECT_0) throw std::runtime_error("GPU timeout");
    void* data;
    Check(readback->Map(0, nullptr, &data), "read depth");
    std::vector<float> values(kSize * kSize);
    for (UINT y = 0; y < kSize; ++y) {
      std::memcpy(values.data() + y * kSize,
                  static_cast<char*>(data) + footprint.Offset + y * footprint.Footprint.RowPitch,
                  kSize * sizeof(float));
    }
    readback->Unmap(0, nullptr);
    return values;
  }
};

int main() {
  try {
    Gpu gpu;
    Translator translator(rex::ui::GraphicsProvider::GpuVendorID::kNvidia, true, false);
    auto vs = Compile(R"(
      cbuffer Constants : register(b0) { float4 c[29]; }
      float4 main(uint id : SV_VertexID) : SV_Position {
        float2 p = id == 0 ? float2(-1,-1) : id == 1 ? float2(-1,3) : float2(3,-1);
        return float4(p, c[0].y + p.x * c[0].z + p.y * c[0].w, 1);
      })", "vs_5_1");
    const Mode modes[]{Mode::kNoModifiers, Mode::kFloat24Truncating, Mode::kFloat24Rounding};
    // Values straddling a float exponent boundary exercise the original
    // hardware-bias discontinuity; small values exercise float24 denormals.
    const float depths[]{0.4999f, 0.5001f, 0.002f, 0.998f, 0.00002f};
    unsigned count = 0;
    for (int mode = 0; mode < 3; ++mode) {
      Translator::Modification modification;
      modification.pixel.depth_bias = 1;
      modification.pixel.depth_stencil_mode = modes[mode];
      auto ps = translator.CreateDepthOnlyPixelShader(modification.value);
      ComPtr<ID3DBlob> disassembly;
      Check(D3DDisassemble(ps.data(), ps.size(), 0, nullptr, &disassembly), "disassemble bias shader");
      std::string assembly_file = "depth-bias-" + std::to_string(mode) + ".txt";
      if (FILE* output = std::fopen(assembly_file.c_str(), "wb")) {
        std::fwrite(disassembly->GetBufferPointer(), 1, disassembly->GetBufferSize(), output);
        std::fclose(output);
      }
      std::string file = "depth-bias-" + std::to_string(mode) + ".dxbc";
      if (FILE* output = std::fopen(file.c_str(), "wb")) {
        std::fwrite(ps.data(), 1, ps.size(), output);
        std::fclose(output);
      }
      for (bool front_ccw : {false, true}) {
        D3D12_GRAPHICS_PIPELINE_STATE_DESC pso{};
        pso.pRootSignature = gpu.root.Get();
        pso.VS = {vs->GetBufferPointer(), vs->GetBufferSize()};
        pso.PS = {ps.data(), ps.size()};
        pso.SampleMask = UINT_MAX;
        pso.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        pso.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        pso.RasterizerState.FrontCounterClockwise = front_ccw;
        pso.RasterizerState.DepthClipEnable = FALSE;
        pso.DepthStencilState.DepthEnable = TRUE;
        pso.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
        pso.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        pso.DSVFormat = DXGI_FORMAT_D32_FLOAT;
        pso.SampleDesc.Count = 1;
        ComPtr<ID3D12PipelineState> pipeline;
        Check(gpu.device->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&pipeline)), "bias pipeline");
        for (float z : depths) for (float dx : {0.0f, 0.03125f}) for (float offset : {-0.0015f, 0.0015f}) {
          const float dy = dx * -0.5f;
          auto values = gpu.Draw(pipeline.Get(), z, dx, dy, -2.0f, offset);
          // Triangle is clockwise in render-target coordinates.
          float face_factor = front_ccw ? -0.5f : 1.0f;
          for (UINT y = 2; y < kSize - 2; ++y) for (UINT x = 2; x < kSize - 2; ++x) {
            float px = (float(x) + 0.5f) * 2.0f / kSize - 1.0f;
            float py = 1.0f - (float(y) + 0.5f) * 2.0f / kSize;
            float interpolated = (z + px * dx + py * dy) * 0.5f;
            float slope = std::max(std::abs(dx), std::abs(dy)) / kSize;
            float expected = std::clamp(interpolated + (slope * -2.0f + offset) * face_factor, 0.0f, 0.5f);
            if (mode) {
              expected = rex::graphics::xenos::Float20e4To32(
                  rex::graphics::xenos::Float32To20e4(expected * 2, mode == 2)) * 0.5f;
            }
            float actual = values[y * kSize + x];
            if (std::abs(actual - expected) > 0.0000003f) {
              std::printf("FAIL mode=%d ccw=%d z=%g dx=%g offset=%g pixel=(%u,%u) expected=%.9g actual=%.9g\n",
                          mode, front_ccw, z, dx, offset, x, y, expected, actual);
              return 1;
            }
          }
          ++count;
        }
      }
    }
    std::printf("PASS: %u GPU draws, both faces, positive/negative bias, slopes, exponent boundaries, float24 modes and viewport clamping.\n", count);
    return 0;
  } catch (const std::exception& e) {
    std::fprintf(stderr, "%s\n", e.what());
    return 1;
  }
}
