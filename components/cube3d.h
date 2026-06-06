#pragma once

// 3-D rotating cube component — requires D3D11 (EUI_D3D11 must be defined).
//
// Usage:
//   static components::Cube3DState cube;
//   cube.yaw   = myYaw;
//   cube.pitch = myPitch;
//   cube.zoom  = myZoom;
//
//   components::cube3d(ui, "my_cube", cube)
//       .size(320.0f, 320.0f)
//       .position(x, y)
//       .onDrag(...)      // any BuilderBase method works
//       .onScroll(...)
//       .onTimer(1.0f/60.0f, []{ })   // drive continuous redraws
//       .build();
//
// Cube3DState owns the D3D11 GPU resources (shaders, buffers, depth buffer).
// It is lazily initialized on the first render call and resizes the depth
// buffer automatically when the window / element size changes.
//
// The cube is rendered *directly into the main render target* at the element's
// layout position using a sub-viewport.  No extra texture copy is needed.

#ifdef EUI_D3D11

#include "core/dsl.h"
#include "core/d3d_context.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdint>

namespace components {

// ============================================================
//  Cube3DState
//  Persistent per-instance state + D3D11 resources.
//  Typically declared as a static local inside compose().
// ============================================================
class Cube3DState {
public:
    // ---- Orientation & view ----
    float yaw   = 0.6f;   // horizontal rotation (radians)
    float pitch = 0.5f;   // vertical rotation (radians)
    float zoom  = 1.0f;   // >1 = zoomed-in (narrower FOV)

    // ---- Render callback (called by CanvasBuilder during the render pass) ----
    void renderCallback(ID3D11DeviceContext* ctx, const core::Rect& frame) {
        if (!initialized_ && !init()) return;
        if (frame.width < 1.0f || frame.height < 1.0f) return;

        auto& g = core::d3d::globalCtx();

        // ---- CPU rotation + per-face diffuse lighting ----
        const float cosY = std::cos(yaw),   sinY = std::sin(yaw);
        const float cosX = std::cos(pitch), sinX = std::sin(pitch);
        // World-space light direction (normalised (2,3,-1))
        constexpr float lx = 0.5345f, ly = 0.8018f, lz = -0.2673f;

        Vertex verts[24];
        for (int f = 0; f < 6; ++f) {
            const FaceInfo& fi = kFaces[f];

            // Rotate face normal: first Y-axis, then X-axis
            const float nx1 =  fi.nx * cosY + fi.nz * sinY;
            const float nz1 = -fi.nx * sinY + fi.nz * cosY;
            const float ny2 =  fi.ny * cosX - nz1 * sinX;
            const float nz2 =  fi.ny * sinX + nz1 * cosX;

            const float ndotl = nx1 * lx + ny2 * ly + nz2 * lz;
            const float lit   = 0.18f + 0.82f * std::max(0.0f, ndotl);

            for (int i = 0; i < 4; ++i) {
                const int vi = fi.v[i];
                const float vx = kVerts[vi][0], vy = kVerts[vi][1], vz = kVerts[vi][2];

                // Rotate vertex position: first Y-axis, then X-axis
                const float x1 =  vx * cosY + vz * sinY;
                const float z1 = -vx * sinY + vz * cosY;
                const float y2 =  vy * cosX - z1 * sinX;
                const float z2 =  vy * sinX + z1 * cosX;

                verts[f * 4 + i] = { x1, y2, z2, fi.r * lit, fi.g * lit, fi.b * lit };
            }
        }

        // Upload vertex data
        {
            D3D11_MAPPED_SUBRESOURCE mapped{};
            ctx->Map(vb_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
            std::memcpy(mapped.pData, verts, sizeof(verts));
            ctx->Unmap(vb_.Get(), 0);
        }

        // ---- Build VP matrix (row-major, row-vector convention) ----
        // Left-handed D3D11 coordinate system.
        // Camera at (0,0,-camZ) looking along +Z.
        // V = translate (0,0,+camZ) — stored in row 3 of row-major matrix.
        // P = standard perspective with aspect-ratio correction.
        const float aspect = frame.width / frame.height;
        const float f_y    = 1.732f * zoom;   // 1/tan(30°) = √3 ≈ 1.732 for fov_y=60°
        const float f_x    = f_y / aspect;    // horizontal FOV factor
        const float nearZ  = 0.5f, farZ = 50.0f, camZ = 4.0f;
        const float P22    =  farZ / (farZ - nearZ);          //  ~1.010
        const float P32    = -nearZ * farZ / (farZ - nearZ);  // ~-0.505

        const float V[16] = {
            1, 0, 0,    0,
            0, 1, 0,    0,
            0, 0, 1,    0,
            0, 0, camZ, 1,   // translation row
        };
        const float P[16] = {
            f_x, 0,   0,   0,
            0,   f_y, 0,   0,
            0,   0,   P22, 1,
            0,   0,   P32, 0,
        };
        float VP[16];
        matMul(VP, V, P);
        core::d3d::updateConstantBuffer(cb_.Get(), VP);

        // ---- Ensure depth buffer matches current render-target size ----
        // Query the bound RTV to find the render-target dimensions.
        // The depth buffer must cover at least (frame.x + frame.width,
        // frame.y + frame.height) because the sub-viewport maps depth
        // writes to physical pixel coordinates in the same coordinate space.
        ComPtr<ID3D11RenderTargetView> curRTV;
        ctx->OMGetRenderTargets(1, &curRTV, nullptr);
        if (!curRTV) return;
        {
            ComPtr<ID3D11Resource>   res;  curRTV->GetResource(&res);
            ComPtr<ID3D11Texture2D>  tex;  res.As(&tex);
            D3D11_TEXTURE2D_DESC     d;    tex->GetDesc(&d);
            ensureDepthBuffer(static_cast<int>(d.Width), static_cast<int>(d.Height));
        }
        if (!dsv_) return;

        // ---- Set sub-viewport = element's physical pixel rect ----
        D3D11_VIEWPORT vp{};
        vp.TopLeftX = frame.x;
        vp.TopLeftY = frame.y;
        vp.Width    = frame.width;
        vp.Height   = frame.height;
        vp.MaxDepth = 1.0f;
        ctx->RSSetViewports(1, &vp);

        // ---- Bind current RTV + own depth buffer, clear depth ----
        ID3D11RenderTargetView* rtvPtr = curRTV.Get();
        ctx->OMSetRenderTargets(1, &rtvPtr, dsv_.Get());
        ctx->OMSetDepthStencilState(dss_.Get(), 0);
        ctx->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFF);  // solid, no alpha blend
        // ClearDepthStencilView clears the entire depth buffer (ignores scissors).
        // This is safe because the depth buffer is only used by this 3-D element.
        ctx->ClearDepthStencilView(dsv_.Get(), D3D11_CLEAR_DEPTH, 1.0f, 0);
        ctx->RSSetState(g.rasterScissorOff.Get());

        // ---- Draw cube ----
        const UINT stride = sizeof(Vertex), offset = 0;
        ctx->IASetInputLayout(layout_.Get());
        ctx->IASetVertexBuffers(0, 1, vb_.GetAddressOf(), &stride, &offset);
        ctx->IASetIndexBuffer(ib_.Get(), DXGI_FORMAT_R16_UINT, 0);
        ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ctx->VSSetShader(vs_.Get(), nullptr, 0);
        ctx->PSSetShader(ps_.Get(), nullptr, 0);
        ctx->VSSetConstantBuffers(0, 1, cb_.GetAddressOf());
        ctx->DrawIndexed(36, 0, 0);
        // Note: renderCanvas() in dsl_runtime.h restores viewport, RTV, and
        // pipeline state after this callback returns.
    }

private:
    // ---- Per-vertex layout ----
    struct Vertex { float x, y, z, r, g, b; };

    // ---- Static geometry ----
    struct FaceInfo { int v[4]; float nx, ny, nz; float r, g, b; };

    static constexpr float kVerts[8][3] = {
        {-1,-1,-1}, { 1,-1,-1}, { 1, 1,-1}, {-1, 1,-1},   // front face (z = -1)
        {-1,-1, 1}, { 1,-1, 1}, { 1, 1, 1}, {-1, 1, 1},   // back  face (z = +1)
    };
    static constexpr FaceInfo kFaces[6] = {
        { {0,1,2,3},  0, 0,-1,  0.91f, 0.30f, 0.36f },   // front  -Z  red
        { {5,4,7,6},  0, 0, 1,  0.30f, 0.62f, 0.91f },   // back   +Z  blue
        { {4,0,3,7}, -1, 0, 0,  0.36f, 0.80f, 0.52f },   // left   -X  green
        { {1,5,6,2},  1, 0, 0,  0.96f, 0.74f, 0.30f },   // right  +X  yellow
        { {4,5,1,0},  0,-1, 0,  0.70f, 0.48f, 0.92f },   // bottom -Y  purple
        { {3,2,6,7},  0, 1, 0,  0.38f, 0.78f, 0.84f },   // top    +Y  teal
    };

    // Row-major 4×4 matrix multiply: out = a * b
    static void matMul(float* out, const float* a, const float* b) {
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j) {
                out[i*4+j] = 0.0f;
                for (int k = 0; k < 4; ++k)
                    out[i*4+j] += a[i*4+k] * b[k*4+j];
            }
    }

    // ---- D3D11 resources ----
    ComPtr<ID3D11DepthStencilView>   dsv_;
    ComPtr<ID3D11DepthStencilState>  dss_;
    ComPtr<ID3D11VertexShader>       vs_;
    ComPtr<ID3D11PixelShader>        ps_;
    ComPtr<ID3D11InputLayout>        layout_;
    ComPtr<ID3D11Buffer>             vb_;   // DYNAMIC, 24 vertices
    ComPtr<ID3D11Buffer>             ib_;   // IMMUTABLE, 36 × uint16
    ComPtr<ID3D11Buffer>             cb_;   // 64 bytes (float4x4 VP)
    bool initialized_ = false;
    int  depthW_ = 0, depthH_ = 0;

    // Create or re-create the depth buffer to match the render target size.
    void ensureDepthBuffer(int w, int h) {
        if (depthW_ == w && depthH_ == h && dsv_) return;
        dsv_.Reset();
        auto& g = core::d3d::globalCtx();
        D3D11_TEXTURE2D_DESC td{};
        td.Width          = static_cast<UINT>(w);
        td.Height         = static_cast<UINT>(h);
        td.MipLevels      = td.ArraySize = 1;
        td.Format         = DXGI_FORMAT_D32_FLOAT;
        td.SampleDesc     = { 1, 0 };
        td.Usage          = D3D11_USAGE_DEFAULT;
        td.BindFlags      = D3D11_BIND_DEPTH_STENCIL;
        ComPtr<ID3D11Texture2D> depthTex;
        if (SUCCEEDED(g.device->CreateTexture2D(&td, nullptr, &depthTex)))
            g.device->CreateDepthStencilView(depthTex.Get(), nullptr, &dsv_);
        depthW_ = w;
        depthH_ = h;
    }

    // Lazy initialise shaders, buffers, and depth-stencil state.
    bool init() {
        auto& g = core::d3d::globalCtx();
        if (!g.device) return false;

        // ---- Depth-stencil state ----
        {
            D3D11_DEPTH_STENCIL_DESC dsd{};
            dsd.DepthEnable    = TRUE;
            dsd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
            dsd.DepthFunc      = D3D11_COMPARISON_LESS;
            if (FAILED(g.device->CreateDepthStencilState(&dsd, &dss_))) return false;
        }

        // ---- HLSL shaders ----
        // row_major float4x4 so that the C++ row-major VP upload matches HLSL.
        // mul(float4(pos,1), vp) = standard row-vector × matrix convention.
        const char* vsSrc =
            "cbuffer CB : register(b0) { row_major float4x4 vp; };\n"
            "struct VS_IN  { float3 p : POSITION; float3 c : COLOR0; };\n"
            "struct VS_OUT { float4 p : SV_Position; float4 c : COLOR0; };\n"
            "VS_OUT main(VS_IN v) {\n"
            "    VS_OUT o;\n"
            "    o.p = mul(float4(v.p, 1.0f), vp);\n"
            "    o.c = float4(v.c, 1.0f);\n"
            "    return o;\n"
            "}\n";
        const char* psSrc =
            "struct PS_IN { float4 p : SV_Position; float4 c : COLOR0; };\n"
            "float4 main(PS_IN i) : SV_Target { return i.c; }\n";

        auto vsBlob = core::d3d::compileShader(vsSrc, "main", "vs_4_0");
        auto psBlob = core::d3d::compileShader(psSrc, "main", "ps_4_0");
        if (!vsBlob || !psBlob) return false;

        if (FAILED(g.device->CreateVertexShader(
                vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(),
                nullptr, &vs_))) return false;
        if (FAILED(g.device->CreatePixelShader(
                psBlob->GetBufferPointer(), psBlob->GetBufferSize(),
                nullptr, &ps_))) return false;

        // ---- Input layout: float3 POSITION + float3 COLOR ----
        D3D11_INPUT_ELEMENT_DESC ied[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0,  0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "COLOR",    0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        };
        if (FAILED(g.device->CreateInputLayout(
                ied, 2, vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(),
                &layout_))) return false;

        // ---- Vertex buffer — DYNAMIC, 24 vertices (4 per face) ----
        {
            D3D11_BUFFER_DESC bd{};
            bd.ByteWidth      = static_cast<UINT>(24 * sizeof(Vertex));
            bd.Usage          = D3D11_USAGE_DYNAMIC;
            bd.BindFlags      = D3D11_BIND_VERTEX_BUFFER;
            bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
            if (FAILED(g.device->CreateBuffer(&bd, nullptr, &vb_))) return false;
        }

        // ---- Index buffer — IMMUTABLE, 36 × uint16 (2 triangles per face) ----
        {
            uint16_t idx[36];
            for (int f = 0; f < 6; ++f) {
                const int b    = f * 6;
                const uint16_t base = static_cast<uint16_t>(f * 4);
                idx[b+0] = base;   idx[b+1] = base+1; idx[b+2] = base+2;
                idx[b+3] = base;   idx[b+4] = base+2; idx[b+5] = base+3;
            }
            D3D11_BUFFER_DESC ibd{};
            ibd.ByteWidth  = sizeof(idx);
            ibd.Usage      = D3D11_USAGE_IMMUTABLE;
            ibd.BindFlags  = D3D11_BIND_INDEX_BUFFER;
            D3D11_SUBRESOURCE_DATA isd{ idx, 0, 0 };
            if (FAILED(g.device->CreateBuffer(&ibd, &isd, &ib_))) return false;
        }

        // ---- Constant buffer — 64 bytes (one float4x4) ----
        cb_ = core::d3d::createConstantBuffer(64);
        if (!cb_) return false;

        initialized_ = true;
        return true;
    }
};

// ============================================================
//  cube3d() — free function that wires a Cube3DState to a
//  Canvas element, returning a CanvasBuilder for further
//  chaining (.size(), .position(), .onDrag(), ...).
// ============================================================
inline core::dsl::CanvasBuilder cube3d(core::dsl::Ui& ui,
                                        const std::string& id,
                                        Cube3DState& state) {
    return ui.canvas(id).onRender(
        [&state](ID3D11DeviceContext* ctx, const core::Rect& frame) {
            state.renderCallback(ctx, frame);
        });
}

} // namespace components

#endif // EUI_D3D11
