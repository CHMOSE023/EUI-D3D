#pragma once

#ifdef EUI_D3D11
#include "core/d3d_context.h"
#include <d3dcompiler.h>
#else
#include <glad/glad.h>
#ifndef GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_NONE
#endif
#include <GLFW/glfw3.h>
#endif

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <vector>

namespace core {

    struct Vec2 {
        float x = 0.0f;
        float y = 0.0f;
    };

    struct Color {
        float r = 1.0f;
        float g = 1.0f;
        float b = 1.0f;
        float a = 1.0f;
    };

    struct Rect {
        float x = 0.0f;
        float y = 0.0f;
        float width = 0.0f;
        float height = 0.0f;

        bool contains(double pointX, double pointY) const {
            return pointX >= x && pointX <= x + width &&
                pointY >= y && pointY <= y + height;
        }
    };

    enum class GradientDirection {
        Horizontal = 0,
        Vertical = 1
    };

    struct Gradient {
        bool enabled = false;
        Color start = { 1.0f, 1.0f, 1.0f, 1.0f };
        Color end = { 1.0f, 1.0f, 1.0f, 1.0f };
        GradientDirection direction = GradientDirection::Vertical;
    };

    struct Border {
        float width = 0.0f;
        Color color = { 1.0f, 1.0f, 1.0f, 1.0f };
    };

    struct Shadow {
        bool enabled = false;
        Vec2 offset = { 0.0f, 4.0f };
        float blur = 8.0f;
        float spread = 0.0f;
        Color color = { 0.0f, 0.0f, 0.0f, 0.28f };
    };

    struct Transform {
        Vec2 translate = { 0.0f, 0.0f };
        Vec2 scale = { 1.0f, 1.0f };
        float rotate = 0.0f;
        Vec2 origin = { 0.5f, 0.5f };
    };

    class RoundedRectPrimitive {
    public:
        RoundedRectPrimitive() = default;

        RoundedRectPrimitive(float x, float y, float width, float height)
            : bounds_{ x, y, width, height } {
        }

        bool initialize() {
#ifdef EUI_D3D11
            return initializeD3D();
        }

        void destroy() {
            if (initialized_) {
                auto& res = d3dSharedResources();
                res.references = std::max(0, res.references - 1);
                initialized_ = false;
            }
        }
#else
            const char* vertexSource =
                "#version 330 core\n"
                "layout(location = 0) in vec2 aScreenPos;\n"
                "layout(location = 1) in vec2 aLocalPos;\n"
                "uniform vec2 uWindowSize;\n"
                "out vec2 vLocalPos;\n"
                "void main() {\n"
                "    vLocalPos = aLocalPos;\n"
                "    vec2 ndc = vec2((aScreenPos.x / uWindowSize.x) * 2.0 - 1.0,\n"
                "                    1.0 - (aScreenPos.y / uWindowSize.y) * 2.0);\n"
                "    gl_Position = vec4(ndc, 0.0, 1.0);\n"
                "}\n";

            const char* fragmentSource =
                "#version 330 core\n"
                "in vec2 vLocalPos;\n"
                "out vec4 FragColor;\n"
                "uniform vec4 uFillColor;\n"
                "uniform vec4 uGradientStart;\n"
                "uniform vec4 uGradientEnd;\n"
                "uniform vec4 uBorderColor;\n"
                "uniform vec4 uShadowColor;\n"
                "uniform vec2 uWindowSize;\n"
                "uniform vec4 uRect;\n"
                "uniform float uRadius;\n"
                "uniform float uBorderWidth;\n"
                "uniform float uOpacity;\n"
                "uniform float uShadowBlur;\n"
                "uniform float uBlurAmount;\n"
                "uniform vec4 uBackdropRect;\n"
                "uniform int uUseGradient;\n"
                "uniform int uGradientDirection;\n"
                "uniform int uShadowPass;\n"
                "uniform sampler2D uBackdrop;\n"
                "float rand(vec2 co) {\n"
                "    return fract(sin(dot(co.xy, vec2(12.9898, 78.233))) * 43758.5453);\n"
                "}\n"
                "float roundedBoxDistance(vec2 point, vec2 halfSize, float radius) {\n"
                "    vec2 cornerVector = abs(point) - halfSize + vec2(radius);\n"
                "    return length(max(cornerVector, 0.0)) + min(max(cornerVector.x, cornerVector.y), 0.0) - radius;\n"
                "}\n"
                "vec3 backdropBlur(vec2 uv) {\n"
                "    vec2 pixelStep = 1.0 / max(uBackdropRect.zw, vec2(1.0));\n"
                "    float blurRadiusPx = uBlurAmount;\n"
                "    vec3 blurred = texture(uBackdrop, uv).rgb;\n"
                "    float repeats = mix(8.0, 24.0, clamp(blurRadiusPx / 36.0, 0.0, 1.0));\n"
                "    const float tau = 6.28318530718;\n"
                "    for (float i = 0.0; i < 24.0; i += 1.0) {\n"
                "        if (i >= repeats) break;\n"
                "        float angle = (i / repeats) * tau;\n"
                "        vec2 dir = vec2(cos(angle), sin(angle));\n"
                "        float radiusA = blurRadiusPx * (0.35 + 0.65 * rand(vec2(i, uv.x + uv.y)));\n"
                "        vec2 uvA = clamp(uv + dir * radiusA * pixelStep, pixelStep * 0.5, vec2(1.0) - pixelStep * 0.5);\n"
                "        blurred += texture(uBackdrop, uvA).rgb;\n"
                "        float angleB = angle + (0.5 * tau / repeats);\n"
                "        vec2 dirB = vec2(cos(angleB), sin(angleB));\n"
                "        float radiusB = blurRadiusPx * (0.20 + 0.80 * rand(vec2(i + 2.0, uv.x + uv.y + 24.0)));\n"
                "        vec2 uvB = clamp(uv + dirB * radiusB * pixelStep, pixelStep * 0.5, vec2(1.0) - pixelStep * 0.5);\n"
                "        blurred += texture(uBackdrop, uvB).rgb;\n"
                "    }\n"
                "    return blurred / (repeats * 2.0 + 1.0);\n"
                "}\n"
                "void main() {\n"
                "    vec2 center = uRect.xy + uRect.zw * 0.5;\n"
                "    float distanceToEdge = roundedBoxDistance(vLocalPos - center, uRect.zw * 0.5, uRadius);\n"
                "    float blur = max(uShadowBlur, 1.0);\n"
                "    if (uShadowPass == 1) {\n"
                "        float shadowAlpha = 1.0 - smoothstep(-blur, blur, distanceToEdge);\n"
                "        if (shadowAlpha <= 0.0) discard;\n"
                "        FragColor = vec4(uShadowColor.rgb, uShadowColor.a * shadowAlpha * uOpacity);\n"
                "        return;\n"
                "    }\n"
                "    float edgeWidth = max(fwidth(distanceToEdge), 0.75);\n"
                "    float shapeAlpha = 1.0 - smoothstep(-edgeWidth, edgeWidth, distanceToEdge);\n"
                "    if (shapeAlpha <= 0.0) discard;\n"
                "    float gradientAmount = uGradientDirection == 0 ?\n"
                "        clamp((vLocalPos.x - uRect.x) / max(uRect.z, 1.0), 0.0, 1.0) :\n"
                "        clamp((vLocalPos.y - uRect.y) / max(uRect.w, 1.0), 0.0, 1.0);\n"
                "    vec4 fill = uUseGradient == 1 ? mix(uGradientStart, uGradientEnd, gradientAmount) : uFillColor;\n"
                "    if (uBlurAmount > 0.0) {\n"
                "        vec2 backdropUv = (gl_FragCoord.xy - uBackdropRect.xy) / max(uBackdropRect.zw, vec2(1.0));\n"
                "        backdropUv = clamp(backdropUv, vec2(0.0), vec2(1.0));\n"
                "        vec3 blurred = backdropBlur(backdropUv);\n"
                "        fill = vec4(mix(blurred, fill.rgb, fill.a), 1.0);\n"
                "    }\n"
                "    float borderAlpha = uBorderWidth > 0.0 ? smoothstep(-uBorderWidth - edgeWidth, -uBorderWidth + edgeWidth, distanceToEdge) : 0.0;\n"
                "    vec4 color = mix(fill, uBorderColor, borderAlpha);\n"
                "    FragColor = vec4(color.rgb, color.a * shapeAlpha * uOpacity);\n"
                "}\n";

            if (!retainSharedResources(vertexSource, fragmentSource)) {
                return false;
            }

            const SharedResources& resources = sharedResources();
            vao_ = resources.vao;
            vbo_ = resources.vbo;
            shaderProgram_ = resources.shaderProgram;
            windowSizeLocation_ = resources.windowSizeLocation;
            fillColorLocation_ = resources.fillColorLocation;
            gradientStartLocation_ = resources.gradientStartLocation;
            gradientEndLocation_ = resources.gradientEndLocation;
            borderColorLocation_ = resources.borderColorLocation;
            shadowColorLocation_ = resources.shadowColorLocation;
            rectLocation_ = resources.rectLocation;
            radiusLocation_ = resources.radiusLocation;
            borderWidthLocation_ = resources.borderWidthLocation;
            opacityLocation_ = resources.opacityLocation;
            shadowBlurLocation_ = resources.shadowBlurLocation;
            blurAmountLocation_ = resources.blurAmountLocation;
            backdropRectLocation_ = resources.backdropRectLocation;
            useGradientLocation_ = resources.useGradientLocation;
            gradientDirectionLocation_ = resources.gradientDirectionLocation;
            shadowPassLocation_ = resources.shadowPassLocation;
            backdropLocation_ = resources.backdropLocation;
            return true;
    }

        void destroy() {
            if (shaderProgram_) {
                releaseSharedResources();
            }
            vbo_ = 0;
            vao_ = 0;
            shaderProgram_ = 0;
            windowSizeLocation_ = -1;
            fillColorLocation_ = -1;
            gradientStartLocation_ = -1;
            gradientEndLocation_ = -1;
            borderColorLocation_ = -1;
            shadowColorLocation_ = -1;
            rectLocation_ = -1;
            radiusLocation_ = -1;
            borderWidthLocation_ = -1;
            opacityLocation_ = -1;
            shadowBlurLocation_ = -1;
            blurAmountLocation_ = -1;
            backdropRectLocation_ = -1;
            useGradientLocation_ = -1;
            gradientDirectionLocation_ = -1;
            shadowPassLocation_ = -1;
            backdropLocation_ = -1;
        }
#endif // EUI_D3D11 initialize/destroy GL path end

        void setBounds(float x, float y, float width, float height) { bounds_ = { x, y, width, height }; }
        void setColor(const Color& color) { color_ = color; }
        void setGradient(const Gradient& gradient) { gradient_ = gradient; }
        void setCornerRadius(float radius) { cornerRadius_ = radius; }
        void setOpacity(float opacity) { opacity_ = std::clamp(opacity, 0.0f, 1.0f); }
        void setBorder(const Border& border) { border_ = border; }
        void setShadow(const Shadow& shadow) { shadow_ = shadow; }
        void setBlur(float blur) { blur_ = std::max(0.0f, blur); }
        void setTranslate(float x, float y) { transform_.translate = { x, y }; }
        void setScale(float x, float y) { transform_.scale = { x, y }; }
        void setRotate(float radians) { transform_.rotate = radians; }
        void setTransformOrigin(float x, float y) { transform_.origin = { x, y }; }
        void setTransform(const Transform& transform) { transform_ = transform; }

        const Rect& bounds() const { return bounds_; }
        const Color& color() const { return color_; }
        const Gradient& gradient() const { return gradient_; }
        const Border& border() const { return border_; }
        const Shadow& shadow() const { return shadow_; }
        float blur() const { return blur_; }
        const Transform& transform() const { return transform_; }
        float cornerRadius() const { return cornerRadius_; }
        float opacity() const { return opacity_; }

        void render(int windowWidth, int windowHeight) const {
#ifdef EUI_D3D11
            if (!initialized_) {
                return;
            }
            D3DSharedResources& res = d3dSharedResources();
            auto& g = core::d3d::globalCtx();
            g.ctx->OMSetBlendState(g.blendState.Get(), nullptr, 0xFFFFFFFF);

            if (blur_ > 0.0f) {
                captureBackdropD3D(windowWidth, windowHeight, bounds_, blur_);
            }
            if (shadow_.enabled) {
                drawShadowD3D(windowWidth, windowHeight, res);
            }
            drawLayerD3D(windowWidth, windowHeight, bounds_, bounds_, false, color_, shadow_.blur, res);
#else
            if (!shaderProgram_ || !vao_ || !vbo_) {
                return;
            }

            const GLboolean blendEnabled = glIsEnabled(GL_BLEND);
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

            if (blur_ > 0.0f) {
                captureBackdrop(windowWidth, windowHeight, bounds_, blur_);
            }

            if (shadow_.enabled) {
                drawShadow(windowWidth, windowHeight);
            }

            drawLayer(windowWidth, windowHeight, bounds_, bounds_, false, color_, shadow_.blur);

            if (!blendEnabled) {
                glDisable(GL_BLEND);
            }
#endif
        }

    private:
#ifdef EUI_D3D11
        struct D3DSharedResources {
            ComPtr<ID3D11VertexShader>       vs;
            ComPtr<ID3D11PixelShader>        ps;
            ComPtr<ID3D11InputLayout>        inputLayout;
            ComPtr<ID3D11Buffer>             vb;
            ComPtr<ID3D11Buffer>             perFrameCB;
            ComPtr<ID3D11Buffer>             perDrawCB;
            ComPtr<ID3D11Texture2D>          backdropTex;
            ComPtr<ID3D11ShaderResourceView> backdropSRV;
            int backdropX = 0, backdropY = 0;
            int backdropWidth = 0, backdropHeight = 0;
            int backdropTexWidth = 0, backdropTexHeight = 0;
            int references = 0;
        };

        static D3DSharedResources& d3dSharedResources() {
            static D3DSharedResources res;
            return res;
        }

        bool initializeD3D() {
            static const char* vsSource =
                "cbuffer PerFrame : register(b0) { float2 windowSize; float2 _p; };\n"
                "struct VS_IN { float2 screenPos : POSITION0; float2 localPos : POSITION1; };\n"
                "struct VS_OUT { float4 pos : SV_Position; float2 localPos : TEXCOORD0; };\n"
                "VS_OUT main(VS_IN v) {\n"
                "    VS_OUT o;\n"
                "    o.localPos = v.localPos;\n"
                "    float2 ndc = float2(v.screenPos.x / windowSize.x * 2.0 - 1.0,\n"
                "                        1.0 - v.screenPos.y / windowSize.y * 2.0);\n"
                "    o.pos = float4(ndc, 0.0, 1.0);\n"
                "    return o;\n"
                "}\n";
            static const char* psSource =
                "Texture2D bdTex : register(t0);\n"
                "SamplerState samp : register(s0);\n"
                "cbuffer PerFrame : register(b0) { float2 wsize0; float2 _p0; };\n"
                "cbuffer PerDraw : register(b1) {\n"
                "    float4 fillColor;\n"
                "    float4 gradientStart;\n"
                "    float4 gradientEnd;\n"
                "    float4 borderColor;\n"
                "    float4 shadowColor;\n"
                "    float4 rect;\n"
                "    float4 backdropRect;\n"
                "    float2 windowSize;\n"
                "    float  radius;\n"
                "    float  borderWidth;\n"
                "    float  opacity;\n"
                "    float  shadowBlur;\n"
                "    float  blurAmount;\n"
                "    int    useGradient;\n"
                "    int    gradientDirection;\n"
                "    int    shadowPass;\n"
                "    float  _pad;\n"
                "    float  _pad2;\n"
                "};\n"
                "float rboxDist(float2 p, float2 hs, float r) {\n"
                "    float2 cv = abs(p) - hs + float2(r, r);\n"
                "    return length(max(cv, 0.0)) + min(max(cv.x, cv.y), 0.0) - r;\n"
                "}\n"
                "float rng(float2 co) { return frac(sin(dot(co, float2(12.9898, 78.233))) * 43758.5453); }\n"
                "float3 bdBlur(float2 uv) {\n"
                "    float2 ps = 1.0 / max(backdropRect.zw, float2(1, 1));\n"
                "    float3 blurred = bdTex.Sample(samp, uv).rgb;\n"
                "    float reps = lerp(8.0, 24.0, saturate(blurAmount / 36.0));\n"
                "    const float tau = 6.28318530718;\n"
                "    [loop] for (float i = 0.0; i < 24.0; i += 1.0) {\n"
                "        if (i >= reps) break;\n"
                "        float angle = (i / reps) * tau;\n"
                "        float2 d = float2(cos(angle), sin(angle));\n"
                "        float ra = blurAmount * (0.35 + 0.65 * rng(float2(i, uv.x + uv.y)));\n"
                "        float2 uvA = clamp(uv + d * ra * ps, ps * 0.5, 1.0 - ps * 0.5);\n"
                "        blurred += bdTex.Sample(samp, uvA).rgb;\n"
                "        float ab = angle + (0.5 * tau / reps);\n"
                "        float2 db = float2(cos(ab), sin(ab));\n"
                "        float rb = blurAmount * (0.20 + 0.80 * rng(float2(i + 2.0, uv.x + uv.y + 24.0)));\n"
                "        float2 uvB = clamp(uv + db * rb * ps, ps * 0.5, 1.0 - ps * 0.5);\n"
                "        blurred += bdTex.Sample(samp, uvB).rgb;\n"
                "    }\n"
                "    return blurred / (reps * 2.0 + 1.0);\n"
                "}\n"
                "struct PS_IN { float4 pos : SV_Position; float2 localPos : TEXCOORD0; };\n"
                "float4 main(PS_IN inp) : SV_Target {\n"
                "    float2 center = rect.xy + rect.zw * 0.5;\n"
                "    float d = rboxDist(inp.localPos - center, rect.zw * 0.5, radius);\n"
                "    float blurVal = max(shadowBlur, 1.0);\n"
                "    if (shadowPass == 1) {\n"
                "        float sa = 1.0 - smoothstep(-blurVal, blurVal, d);\n"
                "        if (sa <= 0.0) discard;\n"
                "        return float4(shadowColor.rgb, shadowColor.a * sa * opacity);\n"
                "    }\n"
                "    float ew = max(abs(ddx(d)) + abs(ddy(d)), 0.75);\n"
                "    float sha = 1.0 - smoothstep(-ew, ew, d);\n"
                "    if (sha <= 0.0) discard;\n"
                "    float gamt = gradientDirection == 0 ?\n"
                "        saturate((inp.localPos.x - rect.x) / max(rect.z, 1.0)) :\n"
                "        saturate((inp.localPos.y - rect.y) / max(rect.w, 1.0));\n"
                "    float4 fill = useGradient == 1 ? lerp(gradientStart, gradientEnd, gamt) : fillColor;\n"
                "    if (blurAmount > 0.0) {\n"
                "        float2 buv = (inp.pos.xy - backdropRect.xy) / max(backdropRect.zw, float2(1, 1));\n"
                "        buv = saturate(buv);\n"
                "        float3 blr = bdBlur(buv);\n"
                "        fill = float4(lerp(blr, fill.rgb, fill.a), 1.0);\n"
                "    }\n"
                "    float ba = borderWidth > 0.0 ? smoothstep(-borderWidth - ew, -borderWidth + ew, d) : 0.0;\n"
                "    float4 col = lerp(fill, borderColor, ba);\n"
                "    return float4(col.rgb, col.a * sha * opacity);\n"
                "}\n";

            auto& g = core::d3d::globalCtx();
            D3DSharedResources& res = d3dSharedResources();
            ++res.references;
            if (res.vs) {
                initialized_ = true;
                return true;
            }

            auto vsBlob = core::d3d::compileShader(vsSource, "main", "vs_5_0");
            auto psBlob = core::d3d::compileShader(psSource, "main", "ps_5_0");
            if (!vsBlob || !psBlob) return false;

            g.device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &res.vs);
            g.device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &res.ps);

            D3D11_INPUT_ELEMENT_DESC layout[] = {
                { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0,  0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
                { "POSITION", 1, DXGI_FORMAT_R32G32_FLOAT, 0,  8, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            };
            g.device->CreateInputLayout(layout, 2, vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), &res.inputLayout);

            res.vb = core::d3d::createDynamicVB(sizeof(float) * 4 * 6);
            res.perFrameCB = core::d3d::createConstantBuffer(16);
            res.perDrawCB = core::d3d::createConstantBuffer(160);

            initialized_ = true;
            return true;
        }

        static void captureBackdropD3D(int windowWidth, int windowHeight, const Rect& bounds, float blur) {
            auto& g = core::d3d::globalCtx();
            D3DSharedResources& res = d3dSharedResources();
            const int safeW = std::max(1, windowWidth);
            const int safeH = std::max(1, windowHeight);
            const int left = std::clamp(static_cast<int>(std::floor(bounds.x - blur)), 0, safeW - 1);
            const int top = std::clamp(static_cast<int>(std::floor(bounds.y - blur)), 0, safeH - 1);
            const int right = std::clamp(static_cast<int>(std::ceil(bounds.x + bounds.width + blur)), left + 1, safeW);
            const int bottom = std::clamp(static_cast<int>(std::ceil(bounds.y + bounds.height + blur)), top + 1, safeH);
            const int capW = right - left;
            const int capH = bottom - top;

            if (!res.backdropTex || res.backdropTexWidth < capW || res.backdropTexHeight < capH) {
                res.backdropTex.Reset();
                res.backdropSRV.Reset();
                D3D11_TEXTURE2D_DESC td{};
                td.Width = (UINT)capW;
                td.Height = (UINT)capH;
                td.MipLevels = 1;
                td.ArraySize = 1;
                td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
                td.SampleDesc.Count = 1;
                td.Usage = D3D11_USAGE_DEFAULT;
                td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
                g.device->CreateTexture2D(&td, nullptr, &res.backdropTex);
                g.device->CreateShaderResourceView(res.backdropTex.Get(), nullptr, &res.backdropSRV);
                res.backdropTexWidth = capW;
                res.backdropTexHeight = capH;
            }
            res.backdropX = left;
            res.backdropY = top;
            res.backdropWidth = capW;
            res.backdropHeight = capH;

            ComPtr<ID3D11RenderTargetView> curRTV;
            g.ctx->OMGetRenderTargets(1, &curRTV, nullptr);
            if (!curRTV) return;
            ComPtr<ID3D11Resource> rtvRes;
            curRTV->GetResource(&rtvRes);
            ComPtr<ID3D11Texture2D> srcTex;
            rtvRes.As(&srcTex);
            if (!srcTex) return;
            D3D11_BOX srcBox{ (UINT)left, (UINT)top, 0, (UINT)right, (UINT)bottom, 1 };
            g.ctx->CopySubresourceRegion(res.backdropTex.Get(), 0, 0, 0, 0, srcTex.Get(), 0, &srcBox);
        }

        void drawLayerD3D(int windowWidth, int windowHeight,
            const Rect& geometryBounds, const Rect& sdfBounds,
            bool shadowPass, const Color& layerColor, float blurVal,
            D3DSharedResources& res) const {
            auto& g = core::d3d::globalCtx();
            const float L = geometryBounds.x;
            const float T = geometryBounds.y;
            const float R = geometryBounds.x + geometryBounds.width;
            const float B = geometryBounds.y + geometryBounds.height;
            const Vec2 p0 = transformPoint(L, T);
            const Vec2 p1 = transformPoint(R, T);
            const Vec2 p2 = transformPoint(R, B);
            const Vec2 p3 = transformPoint(L, B);
            float verts[] = {
                p0.x, p0.y, L, T,
                p1.x, p1.y, R, T,
                p2.x, p2.y, R, B,
                p0.x, p0.y, L, T,
                p2.x, p2.y, R, B,
                p3.x, p3.y, L, B
            };
            core::d3d::uploadVertices(res.vb.Get(), verts, sizeof(verts));

            struct { float wx, wy, p1, p2; } pf{ (float)windowWidth, (float)windowHeight, 0, 0 };
            core::d3d::updateConstantBuffer(res.perFrameCB.Get(), pf);

            const float cr = std::clamp(cornerRadius_, 0.0f, std::min(sdfBounds.width, sdfBounds.height) * 0.5f);
            const float bw = shadowPass ? 0.0f : std::clamp(border_.width, 0.0f, std::min(sdfBounds.width, sdfBounds.height) * 0.5f);

            struct PerDrawData {
                float fillColor[4];
                float gradientStart[4];
                float gradientEnd[4];
                float borderColor[4];
                float shadowColor[4];
                float rect[4];
                float backdropRect[4];
                float windowSizeX, windowSizeY, radius, borderWidth;
                float opacity, shadowBlur, blurAmount;
                int   useGradient;
                int   gradientDirection, shadowPass_;
                float _pad[2];
            } pd{};
            pd.fillColor[0] = color_.r; pd.fillColor[1] = color_.g; pd.fillColor[2] = color_.b; pd.fillColor[3] = color_.a;
            pd.gradientStart[0] = gradient_.start.r; pd.gradientStart[1] = gradient_.start.g;
            pd.gradientStart[2] = gradient_.start.b; pd.gradientStart[3] = gradient_.start.a;
            pd.gradientEnd[0] = gradient_.end.r; pd.gradientEnd[1] = gradient_.end.g;
            pd.gradientEnd[2] = gradient_.end.b; pd.gradientEnd[3] = gradient_.end.a;
            pd.borderColor[0] = border_.color.r; pd.borderColor[1] = border_.color.g;
            pd.borderColor[2] = border_.color.b; pd.borderColor[3] = border_.color.a;
            pd.shadowColor[0] = layerColor.r; pd.shadowColor[1] = layerColor.g;
            pd.shadowColor[2] = layerColor.b; pd.shadowColor[3] = layerColor.a;
            pd.rect[0] = sdfBounds.x; pd.rect[1] = sdfBounds.y;
            pd.rect[2] = sdfBounds.width; pd.rect[3] = sdfBounds.height;
            pd.backdropRect[0] = (float)res.backdropX; pd.backdropRect[1] = (float)res.backdropY;
            pd.backdropRect[2] = (float)std::max(1, res.backdropWidth);
            pd.backdropRect[3] = (float)std::max(1, res.backdropHeight);
            pd.windowSizeX = (float)windowWidth; pd.windowSizeY = (float)windowHeight;
            pd.radius = cr; pd.borderWidth = bw;
            pd.opacity = opacity_; pd.shadowBlur = blurVal; pd.blurAmount = shadowPass ? 0.0f : blur_;
            pd.useGradient = (gradient_.enabled && !shadowPass) ? 1 : 0;
            pd.gradientDirection = static_cast<int>(gradient_.direction);
            pd.shadowPass_ = shadowPass ? 1 : 0;
            core::d3d::updateConstantBuffer(res.perDrawCB.Get(), pd);

            UINT stride = sizeof(float) * 4, offset = 0;
            g.ctx->IASetInputLayout(res.inputLayout.Get());
            g.ctx->IASetVertexBuffers(0, 1, res.vb.GetAddressOf(), &stride, &offset);
            g.ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            g.ctx->VSSetShader(res.vs.Get(), nullptr, 0);
            g.ctx->PSSetShader(res.ps.Get(), nullptr, 0);
            g.ctx->VSSetConstantBuffers(0, 1, res.perFrameCB.GetAddressOf());
            g.ctx->PSSetConstantBuffers(1, 1, res.perDrawCB.GetAddressOf());
            ID3D11ShaderResourceView* srvPtr = res.backdropSRV.Get();
            g.ctx->PSSetShaderResources(0, 1, &srvPtr);
            g.ctx->PSSetSamplers(0, 1, g.linearSampler.GetAddressOf());
            g.ctx->Draw(6, 0);
        }

        void drawShadowD3D(int windowWidth, int windowHeight, D3DSharedResources& res) const {
            if (bounds_.width <= 0.0f || bounds_.height <= 0.0f ||
                opacity_ <= 0.001f || shadow_.color.a <= 0.001f) return;
            Rect ss = bounds_;
            ss.x += shadow_.offset.x - shadow_.spread;
            ss.y += shadow_.offset.y - shadow_.spread;
            ss.width += shadow_.spread * 2.0f;
            ss.height += shadow_.spread * 2.0f;
            const float blurVal = std::max(shadow_.blur, 1.0f);
            const float offMag = std::max(std::fabs(shadow_.offset.x), std::fabs(shadow_.offset.y));
            const float sBlur = blurVal * 1.08f;
            const float sExt = sBlur * 1.18f + offMag * 0.20f + 1.0f;
            drawLayerD3D(windowWidth, windowHeight, expandRect(ss, sExt), ss,
                true, withAlpha(shadow_.color, 0.74f), sBlur, res);
        }

        bool initialized_ = false;
#else
        struct SharedResources {
            GLuint vao = 0;
            GLuint vbo = 0;
            GLuint shaderProgram = 0;
            GLint windowSizeLocation = -1;
            GLint fillColorLocation = -1;
            GLint gradientStartLocation = -1;
            GLint gradientEndLocation = -1;
            GLint borderColorLocation = -1;
            GLint shadowColorLocation = -1;
            GLint rectLocation = -1;
            GLint radiusLocation = -1;
            GLint borderWidthLocation = -1;
            GLint opacityLocation = -1;
            GLint shadowBlurLocation = -1;
            GLint blurAmountLocation = -1;
            GLint backdropRectLocation = -1;
            GLint useGradientLocation = -1;
            GLint gradientDirectionLocation = -1;
            GLint shadowPassLocation = -1;
            GLint backdropLocation = -1;
            GLuint backdropTexture = 0;
            GLuint backdropFramebuffer = 0;
            int backdropX = 0;
            int backdropY = 0;
            int backdropWidth = 0;
            int backdropHeight = 0;
            int backdropTextureWidth = 0;
            int backdropTextureHeight = 0;
            int references = 0;
        };

        static SharedResources& sharedResources() {
            static std::unordered_map<GLFWwindow*, SharedResources> resourcesByContext;
            return resourcesByContext[glfwGetCurrentContext()];
        }

        static bool retainSharedResources(const char* vertexSource, const char* fragmentSource) {
            SharedResources& resources = sharedResources();
            ++resources.references;
            if (resources.shaderProgram != 0) {
                return true;
            }

            GLuint vertexShader = compileShader(GL_VERTEX_SHADER, vertexSource);
            GLuint fragmentShader = compileShader(GL_FRAGMENT_SHADER, fragmentSource);
            if (!vertexShader || !fragmentShader) {
                if (vertexShader) {
                    glDeleteShader(vertexShader);
                }
                if (fragmentShader) {
                    glDeleteShader(fragmentShader);
                }
                resources.references = std::max(0, resources.references - 1);
                return false;
            }

            resources.shaderProgram = glCreateProgram();
            glAttachShader(resources.shaderProgram, vertexShader);
            glAttachShader(resources.shaderProgram, fragmentShader);
            glLinkProgram(resources.shaderProgram);
            glDeleteShader(vertexShader);
            glDeleteShader(fragmentShader);

            GLint linked = 0;
            glGetProgramiv(resources.shaderProgram, GL_LINK_STATUS, &linked);
            if (!linked) {
                glDeleteProgram(resources.shaderProgram);
                resources.shaderProgram = 0;
                resources.references = std::max(0, resources.references - 1);
                return false;
            }

            resources.windowSizeLocation = glGetUniformLocation(resources.shaderProgram, "uWindowSize");
            resources.fillColorLocation = glGetUniformLocation(resources.shaderProgram, "uFillColor");
            resources.gradientStartLocation = glGetUniformLocation(resources.shaderProgram, "uGradientStart");
            resources.gradientEndLocation = glGetUniformLocation(resources.shaderProgram, "uGradientEnd");
            resources.borderColorLocation = glGetUniformLocation(resources.shaderProgram, "uBorderColor");
            resources.shadowColorLocation = glGetUniformLocation(resources.shaderProgram, "uShadowColor");
            resources.rectLocation = glGetUniformLocation(resources.shaderProgram, "uRect");
            resources.radiusLocation = glGetUniformLocation(resources.shaderProgram, "uRadius");
            resources.borderWidthLocation = glGetUniformLocation(resources.shaderProgram, "uBorderWidth");
            resources.opacityLocation = glGetUniformLocation(resources.shaderProgram, "uOpacity");
            resources.shadowBlurLocation = glGetUniformLocation(resources.shaderProgram, "uShadowBlur");
            resources.blurAmountLocation = glGetUniformLocation(resources.shaderProgram, "uBlurAmount");
            resources.backdropRectLocation = glGetUniformLocation(resources.shaderProgram, "uBackdropRect");
            resources.useGradientLocation = glGetUniformLocation(resources.shaderProgram, "uUseGradient");
            resources.gradientDirectionLocation = glGetUniformLocation(resources.shaderProgram, "uGradientDirection");
            resources.shadowPassLocation = glGetUniformLocation(resources.shaderProgram, "uShadowPass");
            resources.backdropLocation = glGetUniformLocation(resources.shaderProgram, "uBackdrop");

            glGenVertexArrays(1, &resources.vao);
            glGenBuffers(1, &resources.vbo);
            glBindVertexArray(resources.vao);
            glBindBuffer(GL_ARRAY_BUFFER, resources.vbo);
            glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 24, nullptr, GL_DYNAMIC_DRAW);
            glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(float) * 4, nullptr);
            glEnableVertexAttribArray(0);
            glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(float) * 4, reinterpret_cast<void*>(sizeof(float) * 2));
            glEnableVertexAttribArray(1);
            glBindBuffer(GL_ARRAY_BUFFER, 0);
            glBindVertexArray(0);

            return resources.shaderProgram != 0 && resources.vao != 0 && resources.vbo != 0;
        }

        static void releaseSharedResources() {
            SharedResources& resources = sharedResources();
            resources.references = std::max(0, resources.references - 1);
            if (resources.references > 0) {
                return;
            }

            if (resources.vbo) {
                glDeleteBuffers(1, &resources.vbo);
                resources.vbo = 0;
            }
            if (resources.vao) {
                glDeleteVertexArrays(1, &resources.vao);
                resources.vao = 0;
            }
            if (resources.shaderProgram) {
                glDeleteProgram(resources.shaderProgram);
                resources.shaderProgram = 0;
            }
            if (resources.backdropTexture) {
                glDeleteTextures(1, &resources.backdropTexture);
                resources.backdropTexture = 0;
            }
            if (resources.backdropFramebuffer) {
                glDeleteFramebuffers(1, &resources.backdropFramebuffer);
                resources.backdropFramebuffer = 0;
            }
            resources.backdropX = 0;
            resources.backdropY = 0;
            resources.backdropWidth = 0;
            resources.backdropHeight = 0;
            resources.backdropTextureWidth = 0;
            resources.backdropTextureHeight = 0;
            resources.windowSizeLocation = -1;
            resources.fillColorLocation = -1;
            resources.gradientStartLocation = -1;
            resources.gradientEndLocation = -1;
            resources.borderColorLocation = -1;
            resources.shadowColorLocation = -1;
            resources.rectLocation = -1;
            resources.radiusLocation = -1;
            resources.borderWidthLocation = -1;
            resources.opacityLocation = -1;
            resources.shadowBlurLocation = -1;
            resources.blurAmountLocation = -1;
            resources.backdropRectLocation = -1;
            resources.useGradientLocation = -1;
            resources.gradientDirectionLocation = -1;
            resources.shadowPassLocation = -1;
            resources.backdropLocation = -1;
        }

        static void ensureBackdropTexture(int width, int height) {
            SharedResources& resources = sharedResources();
            width = std::max(1, width);
            height = std::max(1, height);
            if (resources.backdropTexture != 0 &&
                resources.backdropTextureWidth == width &&
                resources.backdropTextureHeight == height) {
                return;
            }

            if (resources.backdropTexture == 0) {
                glGenTextures(1, &resources.backdropTexture);
            }
            resources.backdropTextureWidth = width;
            resources.backdropTextureHeight = height;
            glBindTexture(GL_TEXTURE_2D, resources.backdropTexture);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glBindTexture(GL_TEXTURE_2D, 0);
        }

        static void captureBackdrop(int windowWidth, int windowHeight, const Rect& bounds, float blur) {
            const int safeWindowWidth = std::max(1, windowWidth);
            const int safeWindowHeight = std::max(1, windowHeight);
            const int left = std::clamp(static_cast<int>(std::floor(bounds.x - blur)), 0, safeWindowWidth - 1);
            const int top = std::clamp(static_cast<int>(std::floor(bounds.y - blur)), 0, safeWindowHeight - 1);
            const int right = std::clamp(static_cast<int>(std::ceil(bounds.x + bounds.width + blur)), left + 1, safeWindowWidth);
            const int bottom = std::clamp(static_cast<int>(std::ceil(bounds.y + bounds.height + blur)), top + 1, safeWindowHeight);
            const int captureWidth = right - left;
            const int captureHeight = bottom - top;
            const int sourceY = safeWindowHeight - bottom;
            constexpr float backdropScale = 0.5f;
            const int textureWidth = std::max(1, static_cast<int>(std::ceil(static_cast<float>(captureWidth) * backdropScale)));
            const int textureHeight = std::max(1, static_cast<int>(std::ceil(static_cast<float>(captureHeight) * backdropScale)));

            ensureBackdropTexture(textureWidth, textureHeight);
            SharedResources& resources = sharedResources();
            resources.backdropX = left;
            resources.backdropY = sourceY;
            resources.backdropWidth = captureWidth;
            resources.backdropHeight = captureHeight;

            if (resources.backdropFramebuffer == 0) {
                glGenFramebuffers(1, &resources.backdropFramebuffer);
            }

            GLint previousReadFramebuffer = 0;
            GLint previousDrawFramebuffer = 0;
            GLint previousTexture = 0;
            glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &previousReadFramebuffer);
            glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &previousDrawFramebuffer);
            glGetIntegerv(GL_TEXTURE_BINDING_2D, &previousTexture);
            const GLboolean scissorEnabled = glIsEnabled(GL_SCISSOR_TEST);

            glBindTexture(GL_TEXTURE_2D, resources.backdropTexture);
            glBindFramebuffer(GL_READ_FRAMEBUFFER, previousDrawFramebuffer);
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, resources.backdropFramebuffer);
            glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, resources.backdropTexture, 0);

            if (glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE) {
                if (scissorEnabled) {
                    glDisable(GL_SCISSOR_TEST);
                }
                glBlitFramebuffer(left, sourceY, right, sourceY + captureHeight,
                    0, 0, textureWidth, textureHeight,
                    GL_COLOR_BUFFER_BIT, GL_LINEAR);
                if (scissorEnabled) {
                    glEnable(GL_SCISSOR_TEST);
                }
            }

            glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(previousReadFramebuffer));
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(previousDrawFramebuffer));
            glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previousTexture));
        }

        static GLuint compileShader(GLenum type, const char* source) {
            GLuint shader = glCreateShader(type);
            glShaderSource(shader, 1, &source, nullptr);
            glCompileShader(shader);

            GLint compiled = 0;
            glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
            if (!compiled) {
                glDeleteShader(shader);
                return 0;
            }

            return shader;
        }

        void drawShadow(int windowWidth, int windowHeight) const {
            if (bounds_.width <= 0.0f || bounds_.height <= 0.0f ||
                opacity_ <= 0.001f || shadow_.color.a <= 0.001f) {
                return;
            }

            Rect shadowShape = bounds_;
            shadowShape.x += shadow_.offset.x - shadow_.spread;
            shadowShape.y += shadow_.offset.y - shadow_.spread;
            shadowShape.width += shadow_.spread * 2.0f;
            shadowShape.height += shadow_.spread * 2.0f;

            const float blur = std::max(shadow_.blur, 1.0f);
            const float offsetMagnitude = std::max(std::fabs(shadow_.offset.x), std::fabs(shadow_.offset.y));
            const float shadowBlur = blur * 1.08f;
            const float shadowExtent = shadowBlur * 1.18f + offsetMagnitude * 0.20f + 1.0f;
            drawLayer(windowWidth, windowHeight, expandRect(shadowShape, shadowExtent), shadowShape,
                true, withAlpha(shadow_.color, 0.74f), shadowBlur);
        }

        void drawLayer(int windowWidth,
            int windowHeight,
            const Rect& geometryBounds,
            const Rect& sdfBounds,
            bool shadowPass,
            const Color& layerColor,
            float blur) const {
            const float left = geometryBounds.x;
            const float top = geometryBounds.y;
            const float right = geometryBounds.x + geometryBounds.width;
            const float bottom = geometryBounds.y + geometryBounds.height;

            const Vec2 p0 = transformPoint(left, top);
            const Vec2 p1 = transformPoint(right, top);
            const Vec2 p2 = transformPoint(right, bottom);
            const Vec2 p3 = transformPoint(left, bottom);

            const float vertices[] = {
                p0.x, p0.y, left, top,
                p1.x, p1.y, right, top,
                p2.x, p2.y, right, bottom,
                p0.x, p0.y, left, top,
                p2.x, p2.y, right, bottom,
                p3.x, p3.y, left, bottom
            };

            const float radius = std::clamp(cornerRadius_, 0.0f, std::min(sdfBounds.width, sdfBounds.height) * 0.5f);
            const float borderWidth = shadowPass ? 0.0f : std::clamp(border_.width, 0.0f, std::min(sdfBounds.width, sdfBounds.height) * 0.5f);

            glUseProgram(shaderProgram_);
            glUniform2f(windowSizeLocation_, static_cast<float>(windowWidth), static_cast<float>(windowHeight));
            glUniform4f(fillColorLocation_, color_.r, color_.g, color_.b, color_.a);
            glUniform4f(gradientStartLocation_, gradient_.start.r, gradient_.start.g, gradient_.start.b, gradient_.start.a);
            glUniform4f(gradientEndLocation_, gradient_.end.r, gradient_.end.g, gradient_.end.b, gradient_.end.a);
            glUniform4f(borderColorLocation_, border_.color.r, border_.color.g, border_.color.b, border_.color.a);
            glUniform4f(shadowColorLocation_, layerColor.r, layerColor.g, layerColor.b, layerColor.a);
            glUniform4f(rectLocation_, sdfBounds.x, sdfBounds.y, sdfBounds.width, sdfBounds.height);
            glUniform1f(radiusLocation_, radius);
            glUniform1f(borderWidthLocation_, borderWidth);
            glUniform1f(opacityLocation_, opacity_);
            glUniform1f(shadowBlurLocation_, blur);
            glUniform1f(blurAmountLocation_, shadowPass ? 0.0f : blur_);
            glUniform4f(backdropRectLocation_,
                static_cast<float>(sharedResources().backdropX),
                static_cast<float>(sharedResources().backdropY),
                static_cast<float>(std::max(1, sharedResources().backdropWidth)),
                static_cast<float>(std::max(1, sharedResources().backdropHeight)));
            glUniform1i(useGradientLocation_, gradient_.enabled && !shadowPass ? 1 : 0);
            glUniform1i(gradientDirectionLocation_, static_cast<int>(gradient_.direction));
            glUniform1i(shadowPassLocation_, shadowPass ? 1 : 0);
            glUniform1i(backdropLocation_, 0);

            if (sharedResources().backdropTexture == 0) {
                ensureBackdropTexture(1, 1);
            }
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, sharedResources().backdropTexture);
            glBindVertexArray(vao_);
            glBindBuffer(GL_ARRAY_BUFFER, vbo_);
            glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices);
            glDrawArrays(GL_TRIANGLES, 0, 6);
            glBindBuffer(GL_ARRAY_BUFFER, 0);
            glBindVertexArray(0);
            glBindTexture(GL_TEXTURE_2D, 0);
        }
#endif // EUI_D3D11

        Vec2 transformPoint(float x, float y) const {
            const Vec2 origin = {
                bounds_.x + bounds_.width * transform_.origin.x,
                bounds_.y + bounds_.height * transform_.origin.y
            };

            const float scaledX = (x - origin.x) * transform_.scale.x;
            const float scaledY = (y - origin.y) * transform_.scale.y;
            const float cosine = std::cos(transform_.rotate);
            const float sine = std::sin(transform_.rotate);

            return {
                origin.x + scaledX * cosine - scaledY * sine + transform_.translate.x,
                origin.y + scaledX * sine + scaledY * cosine + transform_.translate.y
            };
        }

        static Rect expandRect(const Rect& rect, float amount) {
            return {
                rect.x - amount,
                rect.y - amount,
                rect.width + amount * 2.0f,
                rect.height + amount * 2.0f
            };
        }

        static Color withAlpha(Color color, float alphaScale) {
            color.a *= alphaScale;
            return color;
        }

        Rect bounds_;
        Color color_ = { 1.0f, 1.0f, 1.0f, 1.0f };
        Gradient gradient_;
        Border border_;
        Shadow shadow_;
        Transform transform_;
        float cornerRadius_ = 0.0f;
        float blur_ = 0.0f;
        float opacity_ = 1.0f;
#ifndef EUI_D3D11
        GLuint vao_ = 0;
        GLuint vbo_ = 0;
        GLuint shaderProgram_ = 0;
        GLint windowSizeLocation_ = -1;
        GLint fillColorLocation_ = -1;
        GLint gradientStartLocation_ = -1;
        GLint gradientEndLocation_ = -1;
        GLint borderColorLocation_ = -1;
        GLint shadowColorLocation_ = -1;
        GLint rectLocation_ = -1;
        GLint radiusLocation_ = -1;
        GLint borderWidthLocation_ = -1;
        GLint opacityLocation_ = -1;
        GLint shadowBlurLocation_ = -1;
        GLint blurAmountLocation_ = -1;
        GLint backdropRectLocation_ = -1;
        GLint useGradientLocation_ = -1;
        GLint gradientDirectionLocation_ = -1;
        GLint shadowPassLocation_ = -1;
        GLint backdropLocation_ = -1;
#endif
};

    class PolygonPrimitive {
    public:
        PolygonPrimitive() = default;

        bool initialize() {
#ifdef EUI_D3D11
            return initializePolyD3D();
#else
            const char* vertexSource =
                "#version 330 core\n"
                "layout(location = 0) in vec2 aScreenPos;\n"
                "uniform vec2 uWindowSize;\n"
                "void main() {\n"
                "    vec2 ndc = vec2((aScreenPos.x / uWindowSize.x) * 2.0 - 1.0,\n"
                "                    1.0 - (aScreenPos.y / uWindowSize.y) * 2.0);\n"
                "    gl_Position = vec4(ndc, 0.0, 1.0);\n"
                "}\n";

            const char* fragmentSource =
                "#version 330 core\n"
                "out vec4 FragColor;\n"
                "uniform vec4 uFillColor;\n"
                "uniform float uOpacity;\n"
                "void main() {\n"
                "    FragColor = vec4(uFillColor.rgb, uFillColor.a * uOpacity);\n"
                "}\n";

            if (!retainSharedResources(vertexSource, fragmentSource)) {
                return false;
            }

            const SharedResources& resources = sharedResources();
            vao_ = resources.vao;
            vbo_ = resources.vbo;
            shaderProgram_ = resources.shaderProgram;
            windowSizeLocation_ = resources.windowSizeLocation;
            fillColorLocation_ = resources.fillColorLocation;
            opacityLocation_ = resources.opacityLocation;
            return true;
#endif // EUI_D3D11
        }

        void destroy() {
#ifdef EUI_D3D11
            if (polyInitialized_) {
                D3DPolyResources& res = d3dPolyResources();
                res.references = std::max(0, res.references - 1);
                if (res.references == 0) {
                    res.vs.Reset(); res.ps.Reset();
                    res.inputLayout.Reset(); res.vb.Reset();
                    res.perFrameCB.Reset(); res.perDrawCB.Reset();
                }
                polyInitialized_ = false;
            }
#else
            if (shaderProgram_) {
                releaseSharedResources();
            }
            vao_ = 0;
            vbo_ = 0;
            shaderProgram_ = 0;
            windowSizeLocation_ = -1;
            fillColorLocation_ = -1;
            opacityLocation_ = -1;
#endif
        }

        void setBounds(float x, float y, float width, float height) { bounds_ = { x, y, width, height }; }
        void setPoints(const std::vector<Vec2>& points) { points_ = points; }
        void setColor(const Color& color) { color_ = color; }
        void setOpacity(float opacity) { opacity_ = std::clamp(opacity, 0.0f, 1.0f); }
        void setTransform(const Transform& transform) { transform_ = transform; }

        void render(int windowWidth, int windowHeight) const {
#ifdef EUI_D3D11
            if (!polyInitialized_ || points_.size() < 3 || opacity_ <= 0.0f || color_.a <= 0.0f) {
                return;
            }
            // Convert TRIANGLE_FAN to TRIANGLE_LIST
            std::vector<float> verts;
            verts.reserve((points_.size() - 2) * 6u);
            auto addPt = [&](const Vec2& p) {
                const Vec2 t = transformPoint(bounds_.x + p.x, bounds_.y + p.y);
                verts.push_back(t.x); verts.push_back(t.y);
                };
            for (size_t i = 1; i + 1 < points_.size(); ++i) {
                addPt(points_[0]); addPt(points_[i]); addPt(points_[i + 1]);
            }
            D3DPolyResources& res = d3dPolyResources();
            auto& g = core::d3d::globalCtx();

            // Resize VB if needed
            UINT needed = (UINT)(verts.size() * sizeof(float));
            if (needed > res.vbCapacity) {
                res.vb = core::d3d::createDynamicVB(needed * 2);
                res.vbCapacity = needed * 2;
            }
            core::d3d::uploadVertices(res.vb.Get(), verts.data(), needed);

            struct FrameCB { float wx, wy, _px, _py; } fc{ (float)windowWidth, (float)windowHeight, 0, 0 };
            core::d3d::updateConstantBuffer(res.perFrameCB.Get(), fc);
            struct DrawCB { float fillColor[4]; float opacity, _p[3]; } dc{};
            dc.fillColor[0] = color_.r; dc.fillColor[1] = color_.g; dc.fillColor[2] = color_.b; dc.fillColor[3] = color_.a;
            dc.opacity = opacity_;
            core::d3d::updateConstantBuffer(res.perDrawCB.Get(), dc);

            g.ctx->OMSetBlendState(g.blendState.Get(), nullptr, 0xFFFFFFFF);
            UINT stride = sizeof(float) * 2, offset = 0;
            g.ctx->IASetInputLayout(res.inputLayout.Get());
            g.ctx->IASetVertexBuffers(0, 1, res.vb.GetAddressOf(), &stride, &offset);
            g.ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            g.ctx->VSSetShader(res.vs.Get(), nullptr, 0);
            g.ctx->PSSetShader(res.ps.Get(), nullptr, 0);
            g.ctx->VSSetConstantBuffers(0, 1, res.perFrameCB.GetAddressOf());
            g.ctx->PSSetConstantBuffers(1, 1, res.perDrawCB.GetAddressOf());
            g.ctx->Draw((UINT)((points_.size() - 2) * 3), 0);
#else
            if (!shaderProgram_ || !vao_ || !vbo_ || points_.size() < 3 || opacity_ <= 0.0f || color_.a <= 0.0f) {
                return;
            }

            std::vector<float> vertices;
            vertices.reserve(points_.size() * 2u);
            for (const Vec2& point : points_) {
                const Vec2 transformed = transformPoint(bounds_.x + point.x, bounds_.y + point.y);
                vertices.push_back(transformed.x);
                vertices.push_back(transformed.y);
            }

            const GLboolean blendEnabled = glIsEnabled(GL_BLEND);
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

            glUseProgram(shaderProgram_);
            glUniform2f(windowSizeLocation_, static_cast<float>(windowWidth), static_cast<float>(windowHeight));
            glUniform4f(fillColorLocation_, color_.r, color_.g, color_.b, color_.a);
            glUniform1f(opacityLocation_, opacity_);

            glBindVertexArray(vao_);
            glBindBuffer(GL_ARRAY_BUFFER, vbo_);
            glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertices.size() * sizeof(float)), vertices.data(), GL_DYNAMIC_DRAW);
            glDrawArrays(GL_TRIANGLE_FAN, 0, static_cast<GLsizei>(points_.size()));
            glBindBuffer(GL_ARRAY_BUFFER, 0);
            glBindVertexArray(0);

            if (!blendEnabled) {
                glDisable(GL_BLEND);
            }
#endif // EUI_D3D11
        }

    private:
#ifdef EUI_D3D11
        struct D3DPolyResources {
            ComPtr<ID3D11VertexShader> vs;
            ComPtr<ID3D11PixelShader>  ps;
            ComPtr<ID3D11InputLayout>  inputLayout;
            ComPtr<ID3D11Buffer>       vb;
            ComPtr<ID3D11Buffer>       perFrameCB;
            ComPtr<ID3D11Buffer>       perDrawCB;
            UINT vbCapacity = 0;
            int  references = 0;
        };

        static D3DPolyResources& d3dPolyResources() {
            static D3DPolyResources res;
            return res;
        }

        bool initializePolyD3D() {
            D3DPolyResources& res = d3dPolyResources();
            ++res.references;
            polyInitialized_ = true;
            if (res.vs) { return true; }

            auto& g = core::d3d::globalCtx();
            static const char* vsSource = R"(
                 cbuffer PerFrame : register(b0) { float2 windowSize; float2 _p; };
                 float4 main(float2 sp : POSITION0) : SV_Position {
                     float2 ndc=float2(sp.x/windowSize.x*2-1, 1-sp.y/windowSize.y*2);
                     return float4(ndc,0,1);
                 })";
            static const char* psSource = R"(
                 cbuffer PerDraw : register(b1) { float4 fillColor; float opacity; float3 _p; };
                 float4 main() : SV_Target { return float4(fillColor.rgb, fillColor.a*opacity); }
                 )";
            ComPtr<ID3DBlob> vsBlob = core::d3d::compileShader(vsSource, "main", "vs_5_0");
            ComPtr<ID3DBlob> psBlob = core::d3d::compileShader(psSource, "main", "ps_5_0");
            if (!vsBlob || !psBlob) { --res.references; polyInitialized_ = false; return false; }
            g.device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &res.vs);
            g.device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &res.ps);
            D3D11_INPUT_ELEMENT_DESC layout[] = {
                {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0}
            };
            g.device->CreateInputLayout(layout, 1, vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), &res.inputLayout);
            res.perFrameCB = core::d3d::createConstantBuffer(16);
            res.perDrawCB = core::d3d::createConstantBuffer(32);
            return true;
        }

        bool polyInitialized_ = false;
#else
        struct SharedResources {
            GLuint vao = 0;
            GLuint vbo = 0;
            GLuint shaderProgram = 0;
            GLint windowSizeLocation = -1;
            GLint fillColorLocation = -1;
            GLint opacityLocation = -1;
            int references = 0;
        };

        static SharedResources& sharedResources() {
            static std::unordered_map<GLFWwindow*, SharedResources> resourcesByContext;
            return resourcesByContext[glfwGetCurrentContext()];
        }

        static bool retainSharedResources(const char* vertexSource, const char* fragmentSource) {
            SharedResources& resources = sharedResources();
            ++resources.references;
            if (resources.shaderProgram != 0) {
                return true;
            }

            GLuint vertexShader = compileShader(GL_VERTEX_SHADER, vertexSource);
            GLuint fragmentShader = compileShader(GL_FRAGMENT_SHADER, fragmentSource);
            if (!vertexShader || !fragmentShader) {
                if (vertexShader) {
                    glDeleteShader(vertexShader);
                }
                if (fragmentShader) {
                    glDeleteShader(fragmentShader);
                }
                resources.references = std::max(0, resources.references - 1);
                return false;
            }

            resources.shaderProgram = glCreateProgram();
            glAttachShader(resources.shaderProgram, vertexShader);
            glAttachShader(resources.shaderProgram, fragmentShader);
            glLinkProgram(resources.shaderProgram);
            glDeleteShader(vertexShader);
            glDeleteShader(fragmentShader);

            GLint linked = 0;
            glGetProgramiv(resources.shaderProgram, GL_LINK_STATUS, &linked);
            if (!linked) {
                glDeleteProgram(resources.shaderProgram);
                resources.shaderProgram = 0;
                resources.references = std::max(0, resources.references - 1);
                return false;
            }

            resources.windowSizeLocation = glGetUniformLocation(resources.shaderProgram, "uWindowSize");
            resources.fillColorLocation = glGetUniformLocation(resources.shaderProgram, "uFillColor");
            resources.opacityLocation = glGetUniformLocation(resources.shaderProgram, "uOpacity");

            glGenVertexArrays(1, &resources.vao);
            glGenBuffers(1, &resources.vbo);
            glBindVertexArray(resources.vao);
            glBindBuffer(GL_ARRAY_BUFFER, resources.vbo);
            glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(float) * 2, nullptr);
            glEnableVertexAttribArray(0);
            glBindBuffer(GL_ARRAY_BUFFER, 0);
            glBindVertexArray(0);

            return resources.shaderProgram != 0 && resources.vao != 0 && resources.vbo != 0;
        }

        static void releaseSharedResources() {
            SharedResources& resources = sharedResources();
            resources.references = std::max(0, resources.references - 1);
            if (resources.references > 0) {
                return;
            }

            if (resources.vbo) {
                glDeleteBuffers(1, &resources.vbo);
                resources.vbo = 0;
            }
            if (resources.vao) {
                glDeleteVertexArrays(1, &resources.vao);
                resources.vao = 0;
            }
            if (resources.shaderProgram) {
                glDeleteProgram(resources.shaderProgram);
                resources.shaderProgram = 0;
            }
            resources.windowSizeLocation = -1;
            resources.fillColorLocation = -1;
            resources.opacityLocation = -1;
        }

        static GLuint compileShader(GLenum type, const char* source) {
            GLuint shader = glCreateShader(type);
            glShaderSource(shader, 1, &source, nullptr);
            glCompileShader(shader);

            GLint compiled = 0;
            glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
            if (!compiled) {
                glDeleteShader(shader);
                return 0;
            }

            return shader;
        }
#endif // EUI_D3D11

        Vec2 transformPoint(float x, float y) const {
            const Vec2 origin = {
                bounds_.x + bounds_.width * transform_.origin.x,
                bounds_.y + bounds_.height * transform_.origin.y
            };

            const float scaledX = (x - origin.x) * transform_.scale.x;
            const float scaledY = (y - origin.y) * transform_.scale.y;
            const float cosine = std::cos(transform_.rotate);
            const float sine = std::sin(transform_.rotate);

            return {
                origin.x + scaledX * cosine - scaledY * sine + transform_.translate.x,
                origin.y + scaledX * sine + scaledY * cosine + transform_.translate.y
            };
        }

        Rect bounds_;
        std::vector<Vec2> points_;
        Color color_ = { 1.0f, 1.0f, 1.0f, 1.0f };
        Transform transform_;
        float opacity_ = 1.0f;
#ifndef EUI_D3D11
        GLuint vao_ = 0;
        GLuint vbo_ = 0;
        GLuint shaderProgram_ = 0;
        GLint windowSizeLocation_ = -1;
        GLint fillColorLocation_ = -1;
        GLint opacityLocation_ = -1;
#endif
    };

    inline Color mixColor(const Color& from, const Color& to, float amount) {
        const float clampedAmount = std::clamp(amount, 0.0f, 1.0f);
        const float inverse = 1.0f - clampedAmount;
        return {
            from.r * inverse + to.r * clampedAmount,
            from.g * inverse + to.g * clampedAmount,
            from.b * inverse + to.b * clampedAmount,
            from.a * inverse + to.a * clampedAmount
        };
    }

} // namespace core
