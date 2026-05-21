#pragma once

#ifdef EUI_D3D11

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

namespace core::d3d {

    struct D3DContext {
        ComPtr<ID3D11Device>          device;
        ComPtr<ID3D11DeviceContext>   ctx;
        ComPtr<ID3D11BlendState>      blendState;
        ComPtr<ID3D11RasterizerState> rasterScissorOn;
        ComPtr<ID3D11RasterizerState> rasterScissorOff;
        ComPtr<ID3D11SamplerState>    linearSampler;
    };

    struct WindowSwapChain {
        ComPtr<IDXGISwapChain>         swapChain;
        ComPtr<ID3D11RenderTargetView> rtv;
        int width = 0;
        int height = 0;
    };

    inline D3DContext& globalCtx() {
        static D3DContext ctx;
        return ctx;
    }

    inline WindowSwapChain*& currentSwapChainPtr() {
        static WindowSwapChain* ptr = nullptr;
        return ptr;
    }

    inline void setCurrentSwapChain(WindowSwapChain* sc) {
        currentSwapChainPtr() = sc;
    }

    inline WindowSwapChain* currentSwapChain() {
        return currentSwapChainPtr();
    }

    inline bool createDevice() {
        D3DContext& g = globalCtx();

        UINT flags = 0;
#ifdef _DEBUG
        flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

        D3D_FEATURE_LEVEL featureLevel;
        D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0 };
        HRESULT hr = D3D11CreateDevice(
            nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
            flags, levels, 1, D3D11_SDK_VERSION,
            &g.device, &featureLevel, &g.ctx
        );
        if (FAILED(hr)) {
            return false;
        }

        // Alpha-premultiplied-compatible blend state: standard SRC_ALPHA / INV_SRC_ALPHA
        D3D11_BLEND_DESC blendDesc{};
        blendDesc.RenderTarget[0].BlendEnable = TRUE;
        blendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
        blendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
        blendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
        blendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
        blendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
        blendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
        blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        g.device->CreateBlendState(&blendDesc, &g.blendState);

        // Rasterizer with scissor enabled (used during rendering)
        D3D11_RASTERIZER_DESC rasterDesc{};
        rasterDesc.FillMode = D3D11_FILL_SOLID;
        rasterDesc.CullMode = D3D11_CULL_NONE;
        rasterDesc.ScissorEnable = TRUE;
        rasterDesc.DepthClipEnable = TRUE;
        g.device->CreateRasterizerState(&rasterDesc, &g.rasterScissorOn);

        // Rasterizer with scissor disabled (used for full-frame clears / blits)
        rasterDesc.ScissorEnable = FALSE;
        g.device->CreateRasterizerState(&rasterDesc, &g.rasterScissorOff);

        // Linear clamp sampler used by image and text shaders
        D3D11_SAMPLER_DESC sampDesc{};
        sampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
        sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
        sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        sampDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
        sampDesc.MaxLOD = D3D11_FLOAT32_MAX;
        g.device->CreateSamplerState(&sampDesc, &g.linearSampler);

        return true;
    }

    inline WindowSwapChain createSwapChain(HWND hwnd, int w, int h) {
        D3DContext& g = globalCtx();
        WindowSwapChain sc;
        sc.width = w;
        sc.height = h;

        ComPtr<IDXGIDevice>  dxgiDevice;
        ComPtr<IDXGIAdapter> adapter;
        ComPtr<IDXGIFactory> factory;
        g.device->QueryInterface(IID_PPV_ARGS(&dxgiDevice));
        dxgiDevice->GetAdapter(&adapter);
        adapter->GetParent(IID_PPV_ARGS(&factory));

        DXGI_SWAP_CHAIN_DESC desc{};
        desc.BufferCount = 2;
        desc.BufferDesc.Width = (UINT)w;
        desc.BufferDesc.Height = (UINT)h;
        desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.BufferDesc.RefreshRate.Numerator = 0;
        desc.BufferDesc.RefreshRate.Denominator = 1;
        desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.OutputWindow = hwnd;
        desc.SampleDesc.Count = 1;
        desc.SampleDesc.Quality = 0;
        desc.Windowed = TRUE;
        desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
        desc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

        factory->CreateSwapChain(g.device.Get(), &desc, &sc.swapChain);

        ComPtr<ID3D11Texture2D> backBuffer;
        sc.swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
        g.device->CreateRenderTargetView(backBuffer.Get(), nullptr, &sc.rtv);

        return sc;
    }

    inline void resizeSwapChain(WindowSwapChain& sc, int w, int h) {
        if (w <= 0 || h <= 0) {
            return;
        }
        D3DContext& g = globalCtx();

        // Must release RTV before resizing
        sc.rtv.Reset();
        g.ctx->OMSetRenderTargets(0, nullptr, nullptr);

        sc.swapChain->ResizeBuffers(0, (UINT)w, (UINT)h, DXGI_FORMAT_UNKNOWN, 0);

        ComPtr<ID3D11Texture2D> backBuffer;
        sc.swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
        g.device->CreateRenderTargetView(backBuffer.Get(), nullptr, &sc.rtv);

        sc.width = w;
        sc.height = h;
    }

    // Helper: compile an HLSL shader string at runtime.
    // target: "vs_5_0" or "ps_5_0"
    inline ComPtr<ID3DBlob> compileShader(const char* source, const char* entryPoint, const char* target) {
        // d3dcompiler.h must be included by the caller
        ComPtr<ID3DBlob> code;
        ComPtr<ID3DBlob> errors;
        UINT compileFlags = 0;
#ifdef _DEBUG
        compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif
        HRESULT hr = D3DCompile(
            source, strlen(source), nullptr, nullptr, nullptr,
            entryPoint, target, compileFlags, 0, &code, &errors
        );
        if (FAILED(hr)) {
            if (errors) {
                OutputDebugStringA(static_cast<const char*>(errors->GetBufferPointer()));
            }
            return nullptr;
        }
        return code;
    }

    // Helper: create a DYNAMIC vertex buffer of the given byte capacity.
    inline ComPtr<ID3D11Buffer> createDynamicVB(UINT byteCapacity) {
        D3D11_BUFFER_DESC desc{};
        desc.ByteWidth = byteCapacity;
        desc.Usage = D3D11_USAGE_DYNAMIC;
        desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        ComPtr<ID3D11Buffer> buf;
        globalCtx().device->CreateBuffer(&desc, nullptr, &buf);
        return buf;
    }

    // Helper: create an immutable or dynamic constant buffer.
    inline ComPtr<ID3D11Buffer> createConstantBuffer(UINT byteSize) {
        // byteSize must be a multiple of 16
        D3D11_BUFFER_DESC desc{};
        desc.ByteWidth = (byteSize + 15) & ~15u;
        desc.Usage = D3D11_USAGE_DYNAMIC;
        desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        ComPtr<ID3D11Buffer> buf;
        globalCtx().device->CreateBuffer(&desc, nullptr, &buf);
        return buf;
    }

    // Helper: update a constant buffer with new data.
    template <typename T>
    inline void updateConstantBuffer(ID3D11Buffer* cb, const T& data) {
        D3D11_MAPPED_SUBRESOURCE mapped{};
        globalCtx().ctx->Map(cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
        memcpy(mapped.pData, &data, sizeof(T));
        globalCtx().ctx->Unmap(cb, 0);
    }

    // Helper: map a dynamic vertex buffer, copy vertices, unmap.
    inline void uploadVertices(ID3D11Buffer* vb, const void* data, UINT byteSize) {
        D3D11_MAPPED_SUBRESOURCE mapped{};
        globalCtx().ctx->Map(vb, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
        memcpy(mapped.pData, data, byteSize);
        globalCtx().ctx->Unmap(vb, 0);
    }

} // namespace core::d3d

#endif // EUI_D3D11
