#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include "core/text.h"

#ifdef EUI_D3D11
#include "core/d3d_context.h"
#else
#include <glad/glad.h>
#ifndef GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_NONE
#endif
#include <GLFW/glfw3.h>
#endif

#define STB_TRUETYPE_IMPLEMENTATION
#include "3rd/stb_truetype.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace core {

    namespace {

        constexpr int SdfPadding = 12;
        constexpr float SdfOversample = 2.0f;
        constexpr int SdfOnEdgeValue = 128;
        constexpr float SdfPixelDistanceScale = 48.0f;
        constexpr const char* kDefaultUiFontFile = "JingNanJunJunTi-JinNanJunJunTi-Bold-2.ttf";
        constexpr const char* kDefaultIconFontFile = "Font Awesome 7 Free-Solid-900.otf";

        struct FontFace {
            std::string path;
            std::shared_ptr<std::vector<unsigned char>> data;
            stbtt_fontinfo info;
            float scale = 1.0f;
            float ascent = 0.0f;
            float descent = 0.0f;
            float lineGap = 0.0f;
            bool useSdf = true;
        };

        struct FontInfoHolder {
            std::vector<FontFace> faces;
            std::vector<std::string> lazyFallbackPaths;
        };

#ifdef EUI_D3D11
        using Microsoft::WRL::ComPtr;
#endif

        struct SharedTextAtlas {
#ifdef EUI_D3D11
            ComPtr<ID3D11Texture2D>          texture;
            ComPtr<ID3D11ShaderResourceView> srv;
#else
            GLuint texture = 0;
#endif
            int width = 2048;
            int height = 2048;
            int x = 1;
            int y = 1;
            int rowHeight = 0;
            int references = 0;
            std::unordered_map<std::string, TextPrimitive::Glyph> glyphs;
        };

#ifdef EUI_D3D11
        struct SharedTextRenderResources {
            ComPtr<ID3D11VertexShader>  vs;
            ComPtr<ID3D11PixelShader>   ps;
            ComPtr<ID3D11InputLayout>   inputLayout;
            ComPtr<ID3D11Buffer>        vb;
            ComPtr<ID3D11Buffer>        perFrameCB;
            ComPtr<ID3D11Buffer>        perDrawCB;
            UINT vbCapacity = 0;
            int references = 0;
        };
#else
        struct SharedTextRenderResources {
            GLuint vao = 0;
            GLuint vbo = 0;
            GLuint shaderProgram = 0;
            GLint windowSizeLocation = -1;
            GLint colorLocation = -1;
            GLint textureLocation = -1;
            int references = 0;
        };
#endif

        SharedTextAtlas& sharedTextAtlas() {
            static SharedTextAtlas atlas;
            return atlas;
        }

#ifdef EUI_D3D11
        SharedTextRenderResources& sharedTextRenderResources() {
            static SharedTextRenderResources resources;
            return resources;
        }
#else
        SharedTextRenderResources& sharedTextRenderResources() {
            static std::unordered_map<GLFWwindow*, SharedTextRenderResources> resourcesByContext;
            return resourcesByContext[glfwGetCurrentContext()];
        }

        GLuint compileGlShader(GLenum type, const char* source) {
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
#endif

#ifdef EUI_D3D11
        bool createSharedTextRenderResources(SharedTextRenderResources& resources) {
            const char* vsSource =
                "cbuffer PerFrame : register(b0) { float2 windowSize; float2 _pad; };\n"
                "struct VS_IN { float2 pos : POSITION; float2 uv : TEXCOORD0; float useSdf : TEXCOORD1; };\n"
                "struct VS_OUT { float4 pos : SV_Position; float2 uv : TEXCOORD0; float useSdf : TEXCOORD1; };\n"
                "VS_OUT main(VS_IN v) {\n"
                "    VS_OUT o;\n"
                "    o.uv = v.uv;\n"
                "    o.useSdf = v.useSdf;\n"
                "    float2 ndc = float2(v.pos.x / windowSize.x * 2.0 - 1.0,\n"
                "                        1.0 - v.pos.y / windowSize.y * 2.0);\n"
                "    o.pos = float4(ndc, 0.0, 1.0);\n"
                "    return o;\n"
                "}\n";

            const char* psSource =
                "cbuffer PerDraw : register(b1) { float4 color; };\n"
                "Texture2D atlas : register(t0);\n"
                "SamplerState samp : register(s0);\n"
                "struct PS_IN { float4 pos : SV_Position; float2 uv : TEXCOORD0; float useSdf : TEXCOORD1; };\n"
                "float4 main(PS_IN i) : SV_Target {\n"
                "    float sdf = atlas.Sample(samp, i.uv).r;\n"
                "    float fw = abs(ddx(sdf)) + abs(ddy(sdf));\n"
                "    float w = max(fw * 0.7, 0.001);\n"
                "    float alpha = i.useSdf > 0.5 ? smoothstep(0.5 - w, 0.5 + w, sdf) : sdf;\n"
                "    if (alpha <= 0.0) discard;\n"
                "    return float4(color.rgb, color.a * alpha);\n"
                "}\n";

            auto vsBlob = core::d3d::compileShader(vsSource, "main", "vs_4_0");
            auto psBlob = core::d3d::compileShader(psSource, "main", "ps_4_0");
            if (!vsBlob || !psBlob) {
                return false;
            }

            auto& device = core::d3d::globalCtx().device;
            HRESULT hr = device->CreateVertexShader(
                vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &resources.vs);
            if (FAILED(hr)) return false;
            hr = device->CreatePixelShader(
                psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &resources.ps);
            if (FAILED(hr)) return false;

            D3D11_INPUT_ELEMENT_DESC layoutDesc[] = {
                { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0,  0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
                { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0,  8, D3D11_INPUT_PER_VERTEX_DATA, 0 },
                { "TEXCOORD", 1, DXGI_FORMAT_R32_FLOAT,    0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            };
            hr = device->CreateInputLayout(
                layoutDesc, 3, vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), &resources.inputLayout);
            if (FAILED(hr)) return false;

            constexpr UINT kInitialVBBytes = 4096u * 5u * sizeof(float);
            resources.vb = core::d3d::createDynamicVB(kInitialVBBytes);
            resources.vbCapacity = kInitialVBBytes;
            resources.perFrameCB = core::d3d::createConstantBuffer(16); // float2 windowSize + float2 pad
            resources.perDrawCB = core::d3d::createConstantBuffer(16); // float4 color

            return resources.vs && resources.ps && resources.inputLayout &&
                resources.vb && resources.perFrameCB && resources.perDrawCB;
        }
#else
        bool createSharedTextRenderResources(SharedTextRenderResources& resources) {
            const char* vertexSource =
                "#version 330 core\n"
                "layout(location = 0) in vec2 aPos;\n"
                "layout(location = 1) in vec2 aUv;\n"
                "layout(location = 2) in float aUseSdf;\n"
                "uniform vec2 uWindowSize;\n"
                "out vec2 vUv;\n"
                "out float vUseSdf;\n"
                "void main() {\n"
                "    vUv = aUv;\n"
                "    vUseSdf = aUseSdf;\n"
                "    vec2 ndc = vec2((aPos.x / uWindowSize.x) * 2.0 - 1.0,\n"
                "                    1.0 - (aPos.y / uWindowSize.y) * 2.0);\n"
                "    gl_Position = vec4(ndc, 0.0, 1.0);\n"
                "}\n";

            const char* fragmentSource =
                "#version 330 core\n"
                "in vec2 vUv;\n"
                "in float vUseSdf;\n"
                "out vec4 FragColor;\n"
                "uniform sampler2D uAtlas;\n"
                "uniform vec4 uColor;\n"
                "void main() {\n"
                "    float sdf = texture(uAtlas, vUv).r;\n"
                "    float width = max(fwidth(sdf) * 0.7, 0.001);\n"
                "    float alpha = vUseSdf > 0.5 ? smoothstep(0.5 - width, 0.5 + width, sdf) : sdf;\n"
                "    if (alpha <= 0.0) discard;\n"
                "    FragColor = vec4(uColor.rgb, uColor.a * alpha);\n"
                "}\n";

            GLuint vertexShader = compileGlShader(GL_VERTEX_SHADER, vertexSource);
            GLuint fragmentShader = compileGlShader(GL_FRAGMENT_SHADER, fragmentSource);
            if (!vertexShader || !fragmentShader) {
                if (vertexShader) {
                    glDeleteShader(vertexShader);
                }
                if (fragmentShader) {
                    glDeleteShader(fragmentShader);
                }
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
                return false;
            }

            resources.windowSizeLocation = glGetUniformLocation(resources.shaderProgram, "uWindowSize");
            resources.colorLocation = glGetUniformLocation(resources.shaderProgram, "uColor");
            resources.textureLocation = glGetUniformLocation(resources.shaderProgram, "uAtlas");

            glGenVertexArrays(1, &resources.vao);
            glGenBuffers(1, &resources.vbo);
            glBindVertexArray(resources.vao);
            glBindBuffer(GL_ARRAY_BUFFER, resources.vbo);
            glBufferData(GL_ARRAY_BUFFER, 0, nullptr, GL_DYNAMIC_DRAW);
            glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(float) * 5, nullptr);
            glEnableVertexAttribArray(0);
            glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(float) * 5, reinterpret_cast<void*>(sizeof(float) * 2));
            glEnableVertexAttribArray(1);
            glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, sizeof(float) * 5, reinterpret_cast<void*>(sizeof(float) * 4));
            glEnableVertexAttribArray(2);
            glBindBuffer(GL_ARRAY_BUFFER, 0);
            glBindVertexArray(0);

            return resources.shaderProgram != 0 && resources.vao != 0 && resources.vbo != 0;
        }
#endif

        bool retainSharedTextRenderResources() {
            SharedTextRenderResources& resources = sharedTextRenderResources();
            ++resources.references;
#ifdef EUI_D3D11
            if (resources.vs) {
                return true;
            }
#else
            if (resources.shaderProgram != 0) {
                return true;
            }
#endif

            if (createSharedTextRenderResources(resources)) {
                return true;
            }

            resources.references = std::max(0, resources.references - 1);
            return false;
        }

        void releaseSharedTextRenderResources() {
            SharedTextRenderResources& resources = sharedTextRenderResources();
            resources.references = std::max(0, resources.references - 1);
            if (resources.references > 0) {
                return;
            }

#ifdef EUI_D3D11
            resources.vs.Reset();
            resources.ps.Reset();
            resources.inputLayout.Reset();
            resources.vb.Reset();
            resources.perFrameCB.Reset();
            resources.perDrawCB.Reset();
            resources.vbCapacity = 0;
#else
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
            resources.colorLocation = -1;
            resources.textureLocation = -1;
#endif
        }

        bool retainSharedTextAtlas() {
            SharedTextAtlas& atlas = sharedTextAtlas();
            ++atlas.references;
#ifdef EUI_D3D11
            if (atlas.texture) {
                return true;
            }

            D3D11_TEXTURE2D_DESC desc{};
            desc.Width = static_cast<UINT>(atlas.width);
            desc.Height = static_cast<UINT>(atlas.height);
            desc.MipLevels = 1;
            desc.ArraySize = 1;
            desc.Format = DXGI_FORMAT_R8_UNORM;
            desc.SampleDesc.Count = 1;
            desc.Usage = D3D11_USAGE_DEFAULT;
            desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            HRESULT hr = core::d3d::globalCtx().device->CreateTexture2D(&desc, nullptr, &atlas.texture);
            if (FAILED(hr)) {
                return false;
            }

            D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
            srvDesc.Format = DXGI_FORMAT_R8_UNORM;
            srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
            srvDesc.Texture2D.MipLevels = 1;
            hr = core::d3d::globalCtx().device->CreateShaderResourceView(
                atlas.texture.Get(), &srvDesc, &atlas.srv);
            return SUCCEEDED(hr);
#else
            if (atlas.texture != 0) {
                return true;
            }

            glGenTextures(1, &atlas.texture);
            glBindTexture(GL_TEXTURE_2D, atlas.texture);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, atlas.width, atlas.height, 0, GL_RED, GL_UNSIGNED_BYTE, nullptr);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glBindTexture(GL_TEXTURE_2D, 0);
            return atlas.texture != 0;
#endif
        }

        void releaseSharedTextAtlas() {
            SharedTextAtlas& atlas = sharedTextAtlas();
            atlas.references = std::max(0, atlas.references - 1);
#ifdef EUI_D3D11
            if (atlas.references > 0 || !atlas.texture) {
                return;
            }
            atlas.srv.Reset();
            atlas.texture.Reset();
#else
            if (atlas.references > 0 || atlas.texture == 0) {
                return;
            }
            glDeleteTextures(1, &atlas.texture);
            atlas.texture = 0;
#endif
            atlas.x = 1;
            atlas.y = 1;
            atlas.rowHeight = 0;
            atlas.glyphs.clear();
        }

        std::string glyphCacheKey(const FontFace& face, float fontSize, unsigned int codepoint) {
            const int sdfConfig = face.useSdf ? static_cast<int>(std::round(SdfOversample * 100.0f)) : 0;
            return face.path + "#" +
                std::to_string(static_cast<int>(std::round(fontSize * 64.0f))) + "#" +
                (face.useSdf ? "sdf" : "bitmap") + std::to_string(sdfConfig) + "#" +
                std::to_string(codepoint);
        }

        std::string existingPath(const std::filesystem::path& path) {
            std::error_code error;
            if (std::filesystem::exists(path, error)) {
                return path.string();
            }
            return {};
        }

        std::string& defaultUiFontFileOverride() {
            static std::string value;
            return value;
        }

        std::string& defaultIconFontFileOverride() {
            static std::string value;
            return value;
        }

        std::string resolveFontFilePath(const std::string& path);

        std::filesystem::path executableDirectory() {
#ifdef _WIN32
            std::vector<char> buffer(MAX_PATH);
            DWORD length = 0;
            while (true) {
                length = GetModuleFileNameA(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
                if (length == 0) {
                    return {};
                }
                if (length < buffer.size() - 1) {
                    break;
                }
                buffer.resize(buffer.size() * 2);
            }
            return std::filesystem::path(buffer.data()).parent_path();
#else
            return {};
#endif
        }

        std::string resolveProjectAssetPath(const std::string& filename) {
            const std::filesystem::path sourceRoot = std::filesystem::path(__FILE__).parent_path().parent_path();
            const std::filesystem::path exeDir = executableDirectory();
            const std::filesystem::path candidates[] = {
                exeDir / "assets" / filename,
                std::filesystem::path("assets") / filename,
                std::filesystem::path("..") / "assets" / filename,
                std::filesystem::path("..") / ".." / "assets" / filename,
                sourceRoot / "assets" / filename
            };

            for (const auto& candidate : candidates) {
                if (const std::string path = existingPath(candidate); !path.empty()) {
                    return path;
                }
            }
            return (sourceRoot / "assets" / filename).string();
        }

        std::string resolveDefaultUiFontPath() {
            const std::string & override = defaultUiFontFileOverride();
            return override.empty() ? resolveProjectAssetPath(kDefaultUiFontFile) : resolveFontFilePath(override);
        }

        std::string resolveDefaultIconFontPath() {
            const std::string & override = defaultIconFontFileOverride();
            return override.empty() ? resolveProjectAssetPath(kDefaultIconFontFile) : resolveFontFilePath(override);
        }

        std::string resolveFontFilePath(const std::string& path) {
            const std::filesystem::path raw(path);
            if (const std::string existing = existingPath(raw); !existing.empty()) {
                return existing;
            }

            const std::filesystem::path sourceRoot = std::filesystem::path(__FILE__).parent_path().parent_path();
            const std::filesystem::path exeDir = executableDirectory();
            const std::filesystem::path candidates[] = {
                exeDir / "assets" / raw.filename(),
                exeDir / raw,
                std::filesystem::path("assets") / raw.filename(),
                std::filesystem::path("..") / "assets" / raw.filename(),
                std::filesystem::path("..") / ".." / "assets" / raw.filename(),
                sourceRoot / "assets" / raw.filename()
            };

            for (const auto& candidate : candidates) {
                if (const std::string existing = existingPath(candidate); !existing.empty()) {
                    return existing;
                }
            }
            return path;
        }

        bool isFontAwesomePath(const std::string& path) {
            return std::filesystem::path(path).filename().string().find("Font Awesome") != std::string::npos;
        }

        std::shared_ptr<std::vector<unsigned char>> loadSharedFontData(const std::string& fontPath) {
            static std::unordered_map<std::string, std::weak_ptr<std::vector<unsigned char>>> cache;

            if (auto cached = cache[fontPath].lock()) {
                return cached;
            }

            std::ifstream file(fontPath, std::ios::binary);
            if (!file) {
                return {};
            }

            auto data = std::make_shared<std::vector<unsigned char>>(
                std::istreambuf_iterator<char>(file),
                std::istreambuf_iterator<char>());

            if (data->empty()) {
                return {};
            }

            cache[fontPath] = data;
            return data;
        }

        bool loadFontFace(const std::string& path, float fontSize, bool useSdf, FontFace& face) {
            face.path = path;
            face.useSdf = useSdf;
            face.data = loadSharedFontData(path);
            if (!face.data || face.data->empty()) {
                return false;
            }

            const int offset = stbtt_GetFontOffsetForIndex(face.data->data(), 0);
            if (!stbtt_InitFont(&face.info, face.data->data(), offset)) {
                return false;
            }

            face.scale = stbtt_ScaleForPixelHeight(&face.info, fontSize);
            int ascent = 0;
            int descent = 0;
            int lineGap = 0;
            stbtt_GetFontVMetrics(&face.info, &ascent, &descent, &lineGap);
            face.ascent = static_cast<float>(ascent) * face.scale;
            face.descent = static_cast<float>(descent) * face.scale;
            face.lineGap = static_cast<float>(lineGap) * face.scale;
            return true;
        }

        std::string fontStackCacheKey(const std::string& fontPath, float fontSize) {
            return fontPath + "#" + std::to_string(static_cast<int>(std::round(fontSize * 64.0f)));
        }

        std::shared_ptr<FontInfoHolder> loadSharedFontStack(const std::string& fontPath, float fontSize) {
            static std::unordered_map<std::string, std::weak_ptr<FontInfoHolder>> cache;

            const std::string cacheKey = fontStackCacheKey(fontPath, fontSize);
            if (auto cached = cache[cacheKey].lock()) {
                return cached;
            }

            auto holder = std::make_shared<FontInfoHolder>();

            FontFace primary;
            if (!loadFontFace(fontPath, fontSize, !isFontAwesomePath(fontPath), primary)) {
                return {};
            }

            holder->faces.push_back(std::move(primary));

            const std::string assetFallbackPaths[] = {
                resolveDefaultIconFontPath(),
                resolveDefaultUiFontPath()
            };

            for (const std::string& fallbackPath : assetFallbackPaths) {
                if (fallbackPath.empty() || fallbackPath == fontPath) {
                    continue;
                }

                FontFace fallback;
                if (loadFontFace(fallbackPath, fontSize, !isFontAwesomePath(fallbackPath), fallback)) {
                    holder->faces.push_back(std::move(fallback));
                }
            }

#ifdef _WIN32
            holder->lazyFallbackPaths.push_back("C:/Windows/Fonts/msyh.ttc");
#else
            holder->lazyFallbackPaths.push_back("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf");
#endif

            cache[cacheKey] = holder;
            return holder;
        }

    } // namespace

    bool TextPrimitive::initialize() {
        if (!retainSharedTextRenderResources()) {
            return false;
        }

        if (!loadFont() || !retainSharedTextAtlas()) {
            releaseSharedTextRenderResources();
            return false;
        }

#ifdef EUI_D3D11
        initialized_ = true;
#else
        const SharedTextRenderResources& resources = sharedTextRenderResources();
        vao_ = resources.vao;
        vbo_ = resources.vbo;
        shaderProgram_ = resources.shaderProgram;
        windowSizeLocation_ = resources.windowSizeLocation;
        colorLocation_ = resources.colorLocation;
        textureLocation_ = resources.textureLocation;
#endif
        return true;
    }

    void TextPrimitive::destroy() {
#ifdef EUI_D3D11
        if (initialized_) {
            releaseSharedTextAtlas();
            releaseSharedTextRenderResources();
            initialized_ = false;
        }
#else
        if (shaderProgram_) {
            releaseSharedTextAtlas();
            releaseSharedTextRenderResources();
        }
        vbo_ = 0;
        vao_ = 0;
        shaderProgram_ = 0;
        windowSizeLocation_ = -1;
        colorLocation_ = -1;
        textureLocation_ = -1;
#endif
        fontInfoStorage_.reset();
        glyphs_.clear();
        lines_.clear();
        vertices_.clear();
        measuredSize_ = {};
        layoutDirty_ = true;
        verticesDirty_ = true;
        fontDirty_ = true;
    }

    void TextPrimitive::setPosition(float x, float y) {
        if (position_.x == x && position_.y == y) {
            return;
        }
        position_ = { x, y };
        invalidateVertices();
    }

    void TextPrimitive::setText(const std::string& text) {
        if (style_.text == text) {
            return;
        }
        style_.text = text;
        invalidateLayout();
    }

    void TextPrimitive::setFontFamily(const std::string& fontFamily) {
        if (style_.fontFamily == fontFamily) {
            return;
        }
        style_.fontFamily = fontFamily;
        fontDirty_ = true;
        invalidateLayout();
    }

    void TextPrimitive::setFontSize(float fontSize) {
        if (style_.fontSize == fontSize) {
            return;
        }
        style_.fontSize = fontSize;
        fontDirty_ = true;
        invalidateLayout();
    }

    void TextPrimitive::setFontWeight(int fontWeight) {
        if (style_.fontWeight == fontWeight) {
            return;
        }
        style_.fontWeight = fontWeight;
        fontDirty_ = true;
        invalidateLayout();
    }

    void TextPrimitive::setColor(const Color& color) {
        style_.color = color;
    }

    void TextPrimitive::setMaxWidth(float maxWidth) {
        if (style_.maxWidth == maxWidth) {
            return;
        }
        style_.maxWidth = maxWidth;
        invalidateLayout();
    }

    void TextPrimitive::setWrap(bool wrap) {
        if (style_.wrap == wrap) {
            return;
        }
        style_.wrap = wrap;
        invalidateLayout();
    }

    void TextPrimitive::setHorizontalAlign(HorizontalAlign align) {
        style_.horizontalAlign = align;
    }

    void TextPrimitive::setVerticalAlign(VerticalAlign align) {
        style_.verticalAlign = align;
    }

    void TextPrimitive::setLineHeight(float lineHeight) {
        if (style_.lineHeight == lineHeight) {
            return;
        }
        style_.lineHeight = lineHeight;
        invalidateLayout();
    }

    void TextPrimitive::setVisualScale(float originX, float originY, float scale) {
        const float nextScale = std::max(0.01f, scale);
        if (visualScaleOrigin_.x == originX && visualScaleOrigin_.y == originY && visualScale_ == nextScale) {
            return;
        }
        visualScaleOrigin_ = { originX, originY };
        visualScale_ = nextScale;
        invalidateVertices();
    }

    void TextPrimitive::setTransform(const Transform& transform, const Rect& frame) {
        auto close = [](float left, float right) {
            return std::fabs(left - right) <= 0.0001f;
            };
        auto closeVec = [&](const Vec2& left, const Vec2& right) {
            return close(left.x, right.x) && close(left.y, right.y);
            };
        const bool sameTransform =
            closeVec(transform_.translate, transform.translate) &&
            closeVec(transform_.scale, transform.scale) &&
            close(transform_.rotate, transform.rotate) &&
            closeVec(transform_.origin, transform.origin);
        const bool sameFrame =
            close(transformFrame_.x, frame.x) &&
            close(transformFrame_.y, frame.y) &&
            close(transformFrame_.width, frame.width) &&
            close(transformFrame_.height, frame.height);
        if (sameTransform && sameFrame) {
            return;
        }
        transform_ = transform;
        transformFrame_ = frame;
        invalidateVertices();
    }

    void TextPrimitive::setStyle(const TextStyle& style) {
        const bool fontChanged = style.fontFamily != style_.fontFamily ||
            style.fontSize != style_.fontSize ||
            style.fontWeight != style_.fontWeight;
        style_ = style;
        fontDirty_ = fontDirty_ || fontChanged;
        invalidateLayout();
    }

    const TextStyle& TextPrimitive::style() const {
        return style_;
    }

    Vec2 TextPrimitive::position() const {
        return position_;
    }

    Vec2 TextPrimitive::measuredSize() {
        if (layoutDirty_) {
            rebuildLayout();
        }
        return measuredSize_;
    }

    float TextPrimitive::measureTextWidth(const std::string& text,
        const std::string& fontFamily,
        float fontSize,
        int fontWeight) {
        if (text.empty()) {
            return 0.0f;
        }

        const float size = std::max(1.0f, fontSize);
        const std::string fontPath = resolveFontPath(fontFamily, fontWeight);
        auto holder = loadSharedFontStack(fontPath, size);
        if (!holder || holder->faces.empty()) {
            return 0.0f;
        }

        float width = 0.0f;
        size_t index = 0;
        while (index < text.size()) {
            const unsigned int codepoint = readCodepoint(text, index);
            const FontFace* face = &holder->faces.front();
            if (codepoint != ' ' && codepoint != '\t') {
                for (const FontFace& candidate : holder->faces) {
                    if (stbtt_FindGlyphIndex(&candidate.info, static_cast<int>(codepoint)) != 0) {
                        face = &candidate;
                        break;
                    }
                }

                if (stbtt_FindGlyphIndex(&face->info, static_cast<int>(codepoint)) == 0) {
                    for (const std::string& fallbackPath : holder->lazyFallbackPaths) {
                        if (fallbackPath.empty()) {
                            continue;
                        }

                        const bool alreadyLoaded = std::any_of(holder->faces.begin(), holder->faces.end(),
                            [&](const FontFace& loadedFace) {
                                return loadedFace.path == fallbackPath;
                            });
                        if (alreadyLoaded) {
                            continue;
                        }

                        FontFace fallback;
                        if (!loadFontFace(fallbackPath, size, !isFontAwesomePath(fallbackPath), fallback)) {
                            continue;
                        }

                        if (stbtt_FindGlyphIndex(&fallback.info, static_cast<int>(codepoint)) != 0) {
                            holder->faces.push_back(std::move(fallback));
                            face = &holder->faces.back();
                            break;
                        }
                    }
                }
            }

            if (stbtt_FindGlyphIndex(&face->info, static_cast<int>(codepoint)) == 0) {
                width += size * 0.5f;
                continue;
            }

            int advance = 0;
            int leftSideBearing = 0;
            stbtt_GetCodepointHMetrics(&face->info, static_cast<int>(codepoint), &advance, &leftSideBearing);
            width += static_cast<float>(advance) * face->scale;
        }
        return width;
    }

    void TextPrimitive::setDefaultFontFiles(const std::string& textFontFile, const std::string& iconFontFile) {
        defaultUiFontFileOverride() = textFontFile;
        defaultIconFontFileOverride() = iconFontFile;
    }

    void TextPrimitive::render(int windowWidth, int windowHeight) {
#ifdef EUI_D3D11
        if (!initialized_) {
            return;
        }
#else
        if (!shaderProgram_ || !vao_ || !vbo_) {
            return;
        }
#endif

        if (layoutDirty_) {
            rebuildLayout();
        }
        if (verticesDirty_) {
            rebuildVertices();
        }

        if (vertices_.empty()) {
            return;
        }

#ifdef EUI_D3D11
        SharedTextRenderResources& resources = sharedTextRenderResources();
        SharedTextAtlas& atlas = sharedTextAtlas();
        auto& gctx = core::d3d::globalCtx();
        auto* ctx = gctx.ctx.Get();

        const UINT vertexBytes = static_cast<UINT>(vertices_.size() * sizeof(float));
        if (vertexBytes > resources.vbCapacity) {
            resources.vb = core::d3d::createDynamicVB(vertexBytes * 2);
            resources.vbCapacity = vertexBytes * 2;
        }
        core::d3d::uploadVertices(resources.vb.Get(), vertices_.data(), vertexBytes);

        struct PerFrame { float windowSize[2]; float pad[2]; };
        PerFrame perFrame{ { static_cast<float>(windowWidth), static_cast<float>(windowHeight) }, {} };
        core::d3d::updateConstantBuffer(resources.perFrameCB.Get(), perFrame);

        struct PerDraw { float color[4]; };
        PerDraw perDraw{ { style_.color.r, style_.color.g, style_.color.b, style_.color.a } };
        core::d3d::updateConstantBuffer(resources.perDrawCB.Get(), perDraw);

        constexpr UINT stride = sizeof(float) * 5;
        constexpr UINT offset = 0;
        ctx->IASetInputLayout(resources.inputLayout.Get());
        ctx->IASetVertexBuffers(0, 1, resources.vb.GetAddressOf(), &stride, &offset);
        ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ctx->VSSetShader(resources.vs.Get(), nullptr, 0);
        ctx->PSSetShader(resources.ps.Get(), nullptr, 0);
        ctx->VSSetConstantBuffers(0, 1, resources.perFrameCB.GetAddressOf());
        ctx->PSSetConstantBuffers(1, 1, resources.perDrawCB.GetAddressOf());
        ctx->PSSetShaderResources(0, 1, atlas.srv.GetAddressOf());
        ctx->PSSetSamplers(0, 1, gctx.linearSampler.GetAddressOf());
        ctx->OMSetBlendState(gctx.blendState.Get(), nullptr, 0xFFFFFFFF);
        ctx->Draw(static_cast<UINT>(vertices_.size() / 5), 0);
#else
        const GLboolean blendEnabled = glIsEnabled(GL_BLEND);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        glUseProgram(shaderProgram_);
        glUniform2f(windowSizeLocation_, static_cast<float>(windowWidth), static_cast<float>(windowHeight));
        glUniform4f(colorLocation_, style_.color.r, style_.color.g, style_.color.b, style_.color.a);
        glUniform1i(textureLocation_, 0);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, sharedTextAtlas().texture);
        glBindVertexArray(vao_);
        glBindBuffer(GL_ARRAY_BUFFER, vbo_);
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertices_.size() * sizeof(float)), vertices_.data(), GL_DYNAMIC_DRAW);
        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertices_.size() / 5));
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glBindVertexArray(0);
        glBindTexture(GL_TEXTURE_2D, 0);

        if (!blendEnabled) {
            glDisable(GL_BLEND);
        }
#endif
    }

    bool TextPrimitive::loadFont() {
        const std::string fontPath = resolveFontPath(style_.fontFamily, style_.fontWeight);
        auto holder = loadSharedFontStack(fontPath, style_.fontSize);
        if (!holder || holder->faces.empty()) {
            return false;
        }

        fontInfoStorage_ = holder;
        fontData_ = holder->faces.front().data;
        scale_ = holder->faces.front().scale;
        ascent_ = holder->faces.front().ascent;
        descent_ = holder->faces.front().descent;
        lineGap_ = holder->faces.front().lineGap;

        glyphs_.clear();

        fontDirty_ = false;
        return true;
    }

    bool TextPrimitive::ensureGlyph(unsigned int codepoint) {
        if (findGlyph(codepoint)) {
            return true;
        }

        if (fontDirty_ && !loadFont()) {
            return false;
        }

        auto holder = std::static_pointer_cast<FontInfoHolder>(fontInfoStorage_);
        if (!holder || holder->faces.empty()) {
            return false;
        }

        const FontFace* face = &holder->faces.front();
        if (codepoint != ' ' && codepoint != '\t') {
            for (const FontFace& candidate : holder->faces) {
                if (stbtt_FindGlyphIndex(&candidate.info, static_cast<int>(codepoint)) != 0) {
                    face = &candidate;
                    break;
                }
            }

            if (stbtt_FindGlyphIndex(&face->info, static_cast<int>(codepoint)) == 0) {
                for (const std::string& fallbackPath : holder->lazyFallbackPaths) {
                    if (fallbackPath.empty()) {
                        continue;
                    }
                    const bool alreadyLoaded = std::any_of(holder->faces.begin(), holder->faces.end(),
                        [&](const FontFace& loadedFace) {
                            return loadedFace.path == fallbackPath;
                        });
                    if (alreadyLoaded) {
                        continue;
                    }

                    FontFace fallback;
                    if (!loadFontFace(fallbackPath, style_.fontSize, !isFontAwesomePath(fallbackPath), fallback)) {
                        continue;
                    }

                    if (stbtt_FindGlyphIndex(&fallback.info, static_cast<int>(codepoint)) != 0) {
                        holder->faces.push_back(std::move(fallback));
                        face = &holder->faces.back();
                        break;
                    }
                }
            }

            if (stbtt_FindGlyphIndex(&face->info, static_cast<int>(codepoint)) == 0) {
                Glyph missingGlyph;
                missingGlyph.advance = style_.fontSize * 0.5f;
                cacheGlyph(codepoint, missingGlyph);
                return true;
            }
        }

        int advance = 0;
        int leftSideBearing = 0;
        stbtt_GetCodepointHMetrics(&face->info, static_cast<int>(codepoint), &advance, &leftSideBearing);

        Glyph glyph;
        glyph.advance = static_cast<float>(advance) * face->scale;
        glyph.useSdf = face->useSdf;

        if (codepoint == ' ' || codepoint == '\t') {
            cacheGlyph(codepoint, glyph);
            return true;
        }

        const std::string cacheKey = glyphCacheKey(*face, style_.fontSize, codepoint);
        SharedTextAtlas& atlas = sharedTextAtlas();
        if (const auto cached = atlas.glyphs.find(cacheKey); cached != atlas.glyphs.end()) {
            cacheGlyph(codepoint, cached->second);
            return true;
        }

        int width = 0;
        int height = 0;
        int xoff = 0;
        int yoff = 0;
        const float bitmapScale = face->useSdf ? face->scale * SdfOversample : face->scale;
        unsigned char* bitmap = face->useSdf
            ? stbtt_GetCodepointSDF(&face->info, bitmapScale, static_cast<int>(codepoint),
                SdfPadding, SdfOnEdgeValue, SdfPixelDistanceScale, &width, &height, &xoff, &yoff)
            : stbtt_GetCodepointBitmap(&face->info, bitmapScale, bitmapScale, static_cast<int>(codepoint),
                &width, &height, &xoff, &yoff);
        if (!bitmap || width <= 0 || height <= 0) {
            if (bitmap && face->useSdf) {
                stbtt_FreeSDF(bitmap, nullptr);
            }
            else if (bitmap) {
                stbtt_FreeBitmap(bitmap, nullptr);
            }
            cacheGlyph(codepoint, glyph);
            return true;
        }

        if (atlas.x + width + 1 >= atlas.width) {
            atlas.x = 1;
            atlas.y += atlas.rowHeight + 1;
            atlas.rowHeight = 0;
        }
        if (atlas.y + height + 1 >= atlas.height) {
            if (face->useSdf) {
                stbtt_FreeSDF(bitmap, nullptr);
            }
            else {
                stbtt_FreeBitmap(bitmap, nullptr);
            }
            return false;
        }

#ifdef EUI_D3D11
        {
            D3D11_BOX box{ (UINT)atlas.x, (UINT)atlas.y, 0,
                           (UINT)(atlas.x + width), (UINT)(atlas.y + height), 1 };
            core::d3d::globalCtx().ctx->UpdateSubresource(
                atlas.texture.Get(), 0, &box, bitmap, (UINT)width, 0);
        }
#else
        glBindTexture(GL_TEXTURE_2D, atlas.texture);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexSubImage2D(GL_TEXTURE_2D, 0, atlas.x, atlas.y, width, height, GL_RED, GL_UNSIGNED_BYTE, bitmap);
        glBindTexture(GL_TEXTURE_2D, 0);
#endif

        const float layoutScale = face->useSdf ? 1.0f / SdfOversample : 1.0f;
        glyph.xOffset = static_cast<float>(xoff) * layoutScale;
        glyph.yOffset = static_cast<float>(yoff) * layoutScale + face->ascent;
        glyph.width = static_cast<float>(width) * layoutScale;
        glyph.height = static_cast<float>(height) * layoutScale;
        glyph.u0 = static_cast<float>(atlas.x) / static_cast<float>(atlas.width);
        glyph.v0 = static_cast<float>(atlas.y) / static_cast<float>(atlas.height);
        glyph.u1 = static_cast<float>(atlas.x + width) / static_cast<float>(atlas.width);
        glyph.v1 = static_cast<float>(atlas.y + height) / static_cast<float>(atlas.height);
        atlas.glyphs[cacheKey] = glyph;
        cacheGlyph(codepoint, glyph);

        atlas.x += width + 1;
        atlas.rowHeight = std::max(atlas.rowHeight, height);

        if (face->useSdf) {
            stbtt_FreeSDF(bitmap, nullptr);
        }
        else {
            stbtt_FreeBitmap(bitmap, nullptr);
        }
        return true;
    }

    TextPrimitive::Glyph* TextPrimitive::findGlyph(unsigned int codepoint) {
        for (auto& item : glyphs_) {
            if (item.first == codepoint) {
                return &item.second;
            }
        }
        return nullptr;
    }

    void TextPrimitive::cacheGlyph(unsigned int codepoint, const Glyph& glyph) {
        if (Glyph* existing = findGlyph(codepoint)) {
            *existing = glyph;
            return;
        }
        glyphs_.push_back({ codepoint, glyph });
    }

    void TextPrimitive::invalidateLayout() {
        layoutDirty_ = true;
        invalidateVertices();
    }

    void TextPrimitive::rebuildLayout() {
        if (fontDirty_ && !loadFont()) {
            layoutDirty_ = false;
            return;
        }

        lines_.clear();
        measuredSize_ = {};

        Line currentLine;
        float cursorX = 0.0f;
        const float lineHeight = style_.lineHeight > 0.0f ? style_.lineHeight : style_.fontSize * 1.2f;
        const float maxWidth = style_.maxWidth > 0.0f ? style_.maxWidth : 0.0f;

        const std::vector<unsigned int> codepoints = decodeUtf8(style_.text);
        for (unsigned int codepoint : codepoints) {
            if (codepoint == '\r') {
                continue;
            }
            if (codepoint == '\n') {
                measuredSize_.x = std::max(measuredSize_.x, currentLine.width);
                lines_.push_back(currentLine);
                currentLine = {};
                cursorX = 0.0f;
                continue;
            }

            const float advance = glyphAdvance(codepoint);
            if (style_.wrap && maxWidth > 0.0f && cursorX > 0.0f && cursorX + advance > maxWidth) {
                measuredSize_.x = std::max(measuredSize_.x, currentLine.width);
                lines_.push_back(currentLine);
                currentLine = {};
                cursorX = 0.0f;
            }

            appendCodepointToLine(currentLine, codepoint, cursorX);
        }

        measuredSize_.x = std::max(measuredSize_.x, currentLine.width);
        lines_.push_back(currentLine);
        measuredSize_.y = lines_.empty() ? 0.0f : static_cast<float>(lines_.size()) * lineHeight;
        layoutDirty_ = false;
        invalidateVertices();
    }

    void TextPrimitive::invalidateVertices() {
        verticesDirty_ = true;
    }

    void TextPrimitive::rebuildVertices() {
        vertices_.clear();
        const float lineHeight = style_.lineHeight > 0.0f ? style_.lineHeight : style_.fontSize * 1.2f;
        float blockYOffset = 0.0f;
        if (style_.verticalAlign == VerticalAlign::Center) {
            blockYOffset = -measuredSize_.y * 0.5f;
        }
        else if (style_.verticalAlign == VerticalAlign::Bottom) {
            blockYOffset = -measuredSize_.y;
        }

        for (size_t lineIndex = 0; lineIndex < lines_.size(); ++lineIndex) {
            const Line& line = lines_[lineIndex];
            float lineX = position_.x;
            if (style_.horizontalAlign == HorizontalAlign::Center) {
                lineX -= line.width * 0.5f;
            }
            else if (style_.horizontalAlign == HorizontalAlign::Right) {
                lineX -= line.width;
            }

            const float lineY = position_.y + blockYOffset + static_cast<float>(lineIndex) * lineHeight;
            for (const LaidOutGlyph& laidOut : line.glyphs) {
                const Glyph& glyph = laidOut.glyph;
                const float x0 = lineX + laidOut.x + glyph.xOffset;
                const float y0 = lineY + laidOut.y + glyph.yOffset;
                const float x1 = x0 + glyph.width;
                const float y1 = y0 + glyph.height;
                const float useSdf = glyph.useSdf ? 1.0f : 0.0f;
                Vec2 p0{ x0, y0 };
                Vec2 p1{ x1, y0 };
                Vec2 p2{ x1, y1 };
                Vec2 p3{ x0, y1 };

                if (std::fabs(transform_.translate.x) > 0.0001f ||
                    std::fabs(transform_.translate.y) > 0.0001f ||
                    std::fabs(transform_.scale.x - 1.0f) > 0.0001f ||
                    std::fabs(transform_.scale.y - 1.0f) > 0.0001f ||
                    std::fabs(transform_.rotate) > 0.0001f) {
                    const Vec2 origin{
                        transformFrame_.x + transformFrame_.width * transform_.origin.x,
                        transformFrame_.y + transformFrame_.height * transform_.origin.y
                    };
                    const float cosine = std::cos(transform_.rotate);
                    const float sine = std::sin(transform_.rotate);
                    auto transformPoint = [&](Vec2 point) {
                        const float scaledX = (point.x - origin.x) * transform_.scale.x;
                        const float scaledY = (point.y - origin.y) * transform_.scale.y;
                        return Vec2{
                            origin.x + scaledX * cosine - scaledY * sine + transform_.translate.x,
                            origin.y + scaledX * sine + scaledY * cosine + transform_.translate.y
                        };
                        };
                    p0 = transformPoint(p0);
                    p1 = transformPoint(p1);
                    p2 = transformPoint(p2);
                    p3 = transformPoint(p3);
                }

                if (std::fabs(visualScale_ - 1.0f) > 0.0001f) {
                    auto scalePoint = [&](Vec2 point) {
                        return Vec2{
                            visualScaleOrigin_.x + (point.x - visualScaleOrigin_.x) * visualScale_,
                            visualScaleOrigin_.y + (point.y - visualScaleOrigin_.y) * visualScale_
                        };
                        };
                    p0 = scalePoint(p0);
                    p1 = scalePoint(p1);
                    p2 = scalePoint(p2);
                    p3 = scalePoint(p3);
                }

                vertices_.insert(vertices_.end(), {
                    p0.x, p0.y, glyph.u0, glyph.v0, useSdf,
                    p1.x, p1.y, glyph.u1, glyph.v0, useSdf,
                    p2.x, p2.y, glyph.u1, glyph.v1, useSdf,
                    p0.x, p0.y, glyph.u0, glyph.v0, useSdf,
                    p2.x, p2.y, glyph.u1, glyph.v1, useSdf,
                    p3.x, p3.y, glyph.u0, glyph.v1, useSdf
                    });
            }
        }
        verticesDirty_ = false;
    }

    std::vector<unsigned int> TextPrimitive::decodeUtf8(const std::string& text) const {
        std::vector<unsigned int> codepoints;
        size_t index = 0;
        while (index < text.size()) {
            codepoints.push_back(readCodepoint(text, index));
        }
        return codepoints;
    }

    float TextPrimitive::glyphAdvance(unsigned int codepoint) {
        if (!ensureGlyph(codepoint)) {
            return style_.fontSize * 0.5f;
        }
        if (const Glyph* glyph = findGlyph(codepoint)) {
            return glyph->advance;
        }
        return style_.fontSize * 0.5f;
    }

    void TextPrimitive::appendCodepointToLine(Line& line, unsigned int codepoint, float& cursorX) {
        if (!ensureGlyph(codepoint)) {
            return;
        }

        const Glyph* glyph = findGlyph(codepoint);
        if (!glyph) {
            return;
        }
        if (codepoint != ' ' && codepoint != '\t') {
            line.glyphs.push_back({ *glyph, cursorX, 0.0f });
        }
        cursorX += glyph->advance;
        line.width = cursorX;
    }

    unsigned int TextPrimitive::readCodepoint(const std::string& text, size_t& index) {
        const unsigned char first = static_cast<unsigned char>(text[index++]);
        if (first < 0x80) {
            return first;
        }
        if ((first >> 5) == 0x6 && index < text.size()) {
            return ((first & 0x1F) << 6) | (static_cast<unsigned char>(text[index++]) & 0x3F);
        }
        if ((first >> 4) == 0xE && index + 1 < text.size()) {
            unsigned int cp = (first & 0x0F) << 12;
            cp |= (static_cast<unsigned char>(text[index++]) & 0x3F) << 6;
            cp |= static_cast<unsigned char>(text[index++]) & 0x3F;
            return cp;
        }
        if ((first >> 3) == 0x1E && index + 2 < text.size()) {
            unsigned int cp = (first & 0x07) << 18;
            cp |= (static_cast<unsigned char>(text[index++]) & 0x3F) << 12;
            cp |= (static_cast<unsigned char>(text[index++]) & 0x3F) << 6;
            cp |= static_cast<unsigned char>(text[index++]) & 0x3F;
            return cp;
        }
        return '?';
    }

    std::string TextPrimitive::resolveFontPath(const std::string& fontFamily, int fontWeight) {
        if (!fontFamily.empty() && fontFamily.find('.') != std::string::npos) {
            return resolveFontFilePath(fontFamily);
        }

        if (fontFamily == "YouSheBiaoTiHei" || fontFamily == "YouShe") {
            return resolveFontFilePath("YouSheBiaoTiHei-2.ttf");
        }

        if (fontFamily == "Title" || fontFamily == "PingFang" || fontFamily == "PingFang SC") {
            return resolveDefaultUiFontPath();
        }

        if (fontFamily == "FontAwesome" || fontFamily == "Font Awesome" ||
            fontFamily == "Font Awesome 7 Free" || fontFamily == "Icon") {
            return resolveDefaultIconFontPath();
        }

#ifdef _WIN32
        if (fontFamily == "Microsoft YaHei" || fontFamily == "YaHei") {
            return "C:/Windows/Fonts/msyh.ttc";
        }
        if (fontFamily == "SimHei") {
            return "C:/Windows/Fonts/simhei.ttf";
        }
        if (fontWeight >= 600) {
            return resolveDefaultUiFontPath();
        }
        return resolveDefaultUiFontPath();
#else
        (void)fontWeight;
        return resolveDefaultUiFontPath();
#endif
    }

#ifndef EUI_D3D11
    GLuint TextPrimitive::compileShader(GLenum type, const char* source) {
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
#endif

} // namespace core
