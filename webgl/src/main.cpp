#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <string>
#include <unordered_set>
#include <vector>

#include <SDL.h>

#if defined(__EMSCRIPTEN__)
#ifndef IMGUI_IMPL_OPENGL_ES3
#define IMGUI_IMPL_OPENGL_ES3
#endif
#include <GLES3/gl3.h>
#include <emscripten.h>
#else
#include <SDL_opengl.h>
#endif

#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include "imgui_impl_sdl2.h"
#include "imgui_stdlib.h"

namespace
{
    struct Vec3
    {
        float x;
        float y;
        float z;

        static Vec3 Cross(const Vec3& a, const Vec3& b)
        {
            return Vec3{
                a.y * b.z - a.z * b.y,
                a.z * b.x - a.x * b.z,
                a.x * b.y - a.y * b.x};
        }

        static float Dot(const Vec3& a, const Vec3& b)
        {
            return a.x * b.x + a.y * b.y + a.z * b.z;
        }

        static Vec3 Normalize(const Vec3& v)
        {
            const float len = std::sqrt(Dot(v, v));
            if (len <= 1e-6f)
            {
                return Vec3{0.0f, 0.0f, 0.0f};
            }
            const float inv = 1.0f / len;
            return Vec3{v.x * inv, v.y * inv, v.z * inv};
        }

        Vec3 operator+(const Vec3& rhs) const
        {
            return Vec3{x + rhs.x, y + rhs.y, z + rhs.z};
        }

        Vec3 operator-(const Vec3& rhs) const
        {
            return Vec3{x - rhs.x, y - rhs.y, z - rhs.z};
        }

        Vec3 operator*(float scalar) const
        {
            return Vec3{x * scalar, y * scalar, z * scalar};
        }
    };

    struct Mat4
    {
        float m[16];

        static Mat4 Identity()
        {
            Mat4 result{};
            result.m[0] = 1.0f;
            result.m[5] = 1.0f;
            result.m[10] = 1.0f;
            result.m[15] = 1.0f;
            return result;
        }
    };

    Mat4 Multiply(const Mat4& a, const Mat4& b)
    {
        Mat4 result{};
        for (int row = 0; row < 4; ++row)
        {
            for (int col = 0; col < 4; ++col)
            {
                float value = 0.0f;
                for (int k = 0; k < 4; ++k)
                {
                    value += a.m[row * 4 + k] * b.m[k * 4 + col];
                }
                result.m[row * 4 + col] = value;
            }
        }
        return result;
    }

    Mat4 Perspective(float fovRadians, float aspect, float zNear, float zFar)
    {
        const float tanHalf = std::tan(fovRadians * 0.5f);
        Mat4 result{};
        result.m[0] = 1.0f / (aspect * tanHalf);
        result.m[5] = 1.0f / tanHalf;
        result.m[10] = -(zFar + zNear) / (zFar - zNear);
        result.m[11] = -1.0f;
        result.m[14] = -(2.0f * zFar * zNear) / (zFar - zNear);
        return result;
    }

    Mat4 LookAt(const Vec3& eye, const Vec3& target, const Vec3& up)
    {
        const Vec3 f = Vec3::Normalize(target - eye);
        const Vec3 s = Vec3::Normalize(Vec3::Cross(f, up));
        const Vec3 u = Vec3::Cross(s, f);

        Mat4 result = Mat4::Identity();
        result.m[0] = s.x;
        result.m[4] = s.y;
        result.m[8] = s.z;
        result.m[1] = u.x;
        result.m[5] = u.y;
        result.m[9] = u.z;
        result.m[2] = -f.x;
        result.m[6] = -f.y;
        result.m[10] = -f.z;
        result.m[12] = -Vec3::Dot(s, eye);
        result.m[13] = -Vec3::Dot(u, eye);
        result.m[14] = Vec3::Dot(f, eye);
        return result;
    }

    bool Invert(const Mat4& m, Mat4& out)
    {
        const float* src = m.m;
        float inv[16];

        inv[0] = src[5] * src[10] * src[15] - src[5] * src[11] * src[14] - src[9] * src[6] * src[15] + src[9] * src[7] * src[14] + src[13] * src[6] * src[11] - src[13] * src[7] * src[10];
        inv[4] = -src[4] * src[10] * src[15] + src[4] * src[11] * src[14] + src[8] * src[6] * src[15] - src[8] * src[7] * src[14] - src[12] * src[6] * src[11] + src[12] * src[7] * src[10];
        inv[8] = src[4] * src[9] * src[15] - src[4] * src[11] * src[13] - src[8] * src[5] * src[15] + src[8] * src[7] * src[13] + src[12] * src[5] * src[11] - src[12] * src[7] * src[9];
        inv[12] = -src[4] * src[9] * src[14] + src[4] * src[10] * src[13] + src[8] * src[5] * src[14] - src[8] * src[6] * src[13] - src[12] * src[5] * src[10] + src[12] * src[6] * src[9];

        inv[1] = -src[1] * src[10] * src[15] + src[1] * src[11] * src[14] + src[9] * src[2] * src[15] - src[9] * src[3] * src[14] - src[13] * src[2] * src[11] + src[13] * src[3] * src[10];
        inv[5] = src[0] * src[10] * src[15] - src[0] * src[11] * src[14] - src[8] * src[2] * src[15] + src[8] * src[3] * src[14] + src[12] * src[2] * src[11] - src[12] * src[3] * src[10];
        inv[9] = -src[0] * src[9] * src[15] + src[0] * src[11] * src[13] + src[8] * src[1] * src[15] - src[8] * src[3] * src[13] - src[12] * src[1] * src[11] + src[12] * src[3] * src[9];
        inv[13] = src[0] * src[9] * src[14] - src[0] * src[10] * src[13] - src[8] * src[1] * src[14] + src[8] * src[2] * src[13] + src[12] * src[1] * src[10] - src[12] * src[2] * src[9];

        inv[2] = src[1] * src[6] * src[15] - src[1] * src[7] * src[14] - src[5] * src[2] * src[15] + src[5] * src[3] * src[14] + src[13] * src[2] * src[7] - src[13] * src[3] * src[6];
        inv[6] = -src[0] * src[6] * src[15] + src[0] * src[7] * src[14] + src[4] * src[2] * src[15] - src[4] * src[3] * src[14] - src[12] * src[2] * src[7] + src[12] * src[3] * src[6];
        inv[10] = src[0] * src[5] * src[15] - src[0] * src[7] * src[13] - src[4] * src[1] * src[15] + src[4] * src[3] * src[13] + src[12] * src[1] * src[7] - src[12] * src[3] * src[5];
        inv[14] = -src[0] * src[5] * src[14] + src[0] * src[6] * src[13] + src[4] * src[1] * src[14] - src[4] * src[2] * src[13] - src[12] * src[1] * src[6] + src[12] * src[2] * src[5];

        inv[3] = -src[1] * src[6] * src[11] + src[1] * src[7] * src[10] + src[5] * src[2] * src[11] - src[5] * src[3] * src[10] - src[9] * src[2] * src[7] + src[9] * src[3] * src[6];
        inv[7] = src[0] * src[6] * src[11] - src[0] * src[7] * src[10] - src[4] * src[2] * src[11] + src[4] * src[3] * src[10] + src[8] * src[2] * src[7] - src[8] * src[3] * src[6];
        inv[11] = -src[0] * src[5] * src[11] + src[0] * src[7] * src[9] + src[4] * src[1] * src[11] - src[4] * src[3] * src[9] - src[8] * src[1] * src[7] + src[8] * src[3] * src[5];
        inv[15] = src[0] * src[5] * src[10] - src[0] * src[6] * src[9] - src[4] * src[1] * src[10] + src[4] * src[2] * src[9] + src[8] * src[1] * src[6] - src[8] * src[2] * src[5];

        float det = src[0] * inv[0] + src[1] * inv[4] + src[2] * inv[8] + src[3] * inv[12];
        if (std::fabs(det) < 1e-8f)
        {
            return false;
        }
        det = 1.0f / det;
        for (int i = 0; i < 16; ++i)
        {
            out.m[i] = inv[i] * det;
        }
        return true;
    }

    Vec3 TransformPoint(const Mat4& m, const Vec3& v)
    {
        const float x = m.m[0] * v.x + m.m[4] * v.y + m.m[8] * v.z + m.m[12];
        const float y = m.m[1] * v.x + m.m[5] * v.y + m.m[9] * v.z + m.m[13];
        const float z = m.m[2] * v.x + m.m[6] * v.y + m.m[10] * v.z + m.m[14];
        const float w = m.m[3] * v.x + m.m[7] * v.y + m.m[11] * v.z + m.m[15];
        if (w != 0.0f)
        {
            return Vec3{x / w, y / w, z / w};
        }
        return Vec3{x, y, z};
    }

    struct PlacedCube
    {
        int gridX;
        int gridZ;
        float r;
        float g;
        float b;
        bool glowing = false;
    };

    struct SpawnPreset
    {
        const char* name;
        float r;
        float g;
        float b;
        bool glowing;
    };

    constexpr SpawnPreset kSpawnPresets[] = {
        {"Blue Cube", 0.30f, 0.45f, 0.85f, false},
        {"Red Cube", 0.85f, 0.35f, 0.35f, false},
        {"Grey Cube", 0.65f, 0.65f, 0.65f, false},
        {"Glow Cube", 1.0f, 0.92f, 0.50f, true},
    };
    constexpr int kMaxRaytraceCubes = 64;

    struct AppState
    {
        SDL_Window* window = nullptr;
        SDL_GLContext glContext = nullptr;
        bool running = true;

        int windowWidth = 1280;
        int windowHeight = 720;

        float deltaTime = 0.016f;
        double previousTime = 0.0;

        Vec3 cameraFocus{0.0f, 0.0f, 0.0f};
        float cameraDistance = 8.0f;
        bool cameraForward = false;
        bool cameraBackward = false;
        bool cameraLeft = false;
        bool cameraRight = false;
        Vec3 cameraEye{0.0f, 6.0f, 8.0f};
        Vec3 cameraDir{0.0f, -0.7f, -1.0f};
        Vec3 cameraRightVec{1.0f, 0.0f, 0.0f};
        Vec3 cameraUpVec{0.0f, 1.0f, 0.0f};

        std::string codeContent =
            "-- Lua script\n"
            "function tick(dt)\n"
            "    -- update logic here\n"
            "end\n";
        bool codeDirty = false;
        std::string docsContent =
            "VEngine WebGL Docs\n"
            "==================\n\n"
            "Controls:\n"
            "  WASD  - move camera\n"
            "  Wheel - zoom camera\n"
            "  LMB   - place cube\n"
            "  RMB   - remove cube\n\n"
            "Overlays:\n"
            "  Code     - edit lua scripts (auto indent, 4 spaces)\n"
            "  Docs     - read-only documentation (copy button)\n"
            "  Content  - choose cube presets (Glow Cube emits light)\n"
            "  Compile  - raytraced preview of lighting\n";

        bool showDocs = false;
        bool showContent = false;

        std::vector<PlacedCube> cubes;
        int selectedPreset = 2;

        GLuint cubeVao = 0;
        GLuint cubeVbo = 0;
        GLuint cubeEbo = 0;
        GLuint gridVao = 0;
        GLuint gridVbo = 0;
        GLuint backgroundVao = 0;
        GLuint backgroundVbo = 0;
        GLuint litProgram = 0;
        GLuint gridProgram = 0;
        GLuint backgroundProgram = 0;
        GLuint glowVao = 0;
        GLuint glowVbo = 0;
        GLuint glowProgram = 0;
        int glowFanVertexCount = 0;
        bool showRaytrace = false;
        GLuint raytraceTexture = 0;
        GLuint raytraceFbo = 0;
        GLuint raytraceProgram = 0;
        int raytraceWidth = 512;
        int raytraceHeight = 512;
        GLint raytraceLocResolution = -1;
        GLint raytraceLocCameraPos = -1;
        GLint raytraceLocCameraDir = -1;
        GLint raytraceLocCameraRight = -1;
        GLint raytraceLocCameraUp = -1;
        GLint raytraceLocFovTan = -1;
        GLint raytraceLocCubeCount = -1;
        GLint raytraceLocCubeData = -1;
        GLint raytraceLocCubeColor = -1;
        GLint raytraceLocCubeGlow = -1;

        Mat4 projection{};
        Mat4 view{};
    };

    SpawnPreset GetPreset(int index)
    {
        index = std::clamp(index, 0, static_cast<int>(std::size(kSpawnPresets)) - 1);
        return kSpawnPresets[index];
    }

    GLuint CompileShader(GLenum type, const char* source)
    {
        const GLuint shader = glCreateShader(type);
        glShaderSource(shader, 1, &source, nullptr);
        glCompileShader(shader);
        GLint success = 0;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
        if (!success)
        {
            char log[1024];
            glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
            SDL_Log("Shader compile error: %s", log);
            glDeleteShader(shader);
            return 0;
        }
        return shader;
    }

    GLuint LinkProgram(GLuint vs, GLuint fs)
    {
        const GLuint program = glCreateProgram();
        glAttachShader(program, vs);
        glAttachShader(program, fs);
        glLinkProgram(program);
        GLint success = 0;
        glGetProgramiv(program, GL_LINK_STATUS, &success);
        if (!success)
        {
            char log[1024];
            glGetProgramInfoLog(program, sizeof(log), nullptr, log);
            SDL_Log("Program link error: %s", log);
            glDeleteProgram(program);
            return 0;
        }
        return program;
    }

    void CreateBackground(AppState& app)
    {
        const float vertices[] = {
            -1.0f, -1.0f,
            3.0f, -1.0f,
            -1.0f, 3.0f};
        glGenVertexArrays(1, &app.backgroundVao);
        glGenBuffers(1, &app.backgroundVbo);
        glBindVertexArray(app.backgroundVao);
        glBindBuffer(GL_ARRAY_BUFFER, app.backgroundVbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(float) * 2, nullptr);
        glBindVertexArray(0);

        const char* vsSource =
            "#version 300 es\n"
            "layout(location = 0) in vec2 aPos;\n"
            "out vec2 vUV;\n"
            "void main() {\n"
            "    vUV = aPos * 0.5 + 0.5;\n"
            "    gl_Position = vec4(aPos, 0.0, 1.0);\n"
            "}\n";

        const char* fsSource =
            "#version 300 es\n"
            "precision mediump float;\n"
            "in vec2 vUV;\n"
            "out vec4 FragColor;\n"
            "void main() {\n"
            "    vec3 top = vec3(0.18, 0.13, 0.25);\n"
            "    vec3 bottom = vec3(0.03, 0.05, 0.12);\n"
            "    float t = clamp(vUV.y, 0.0, 1.0);\n"
            "    vec3 color = mix(top, bottom, t);\n"
            "    FragColor = vec4(color, 1.0);\n"
            "}\n";

        GLuint vs = CompileShader(GL_VERTEX_SHADER, vsSource);
        GLuint fs = CompileShader(GL_FRAGMENT_SHADER, fsSource);
        app.backgroundProgram = LinkProgram(vs, fs);
        glDeleteShader(vs);
        glDeleteShader(fs);
    }

    void RenderGradientBackground(AppState& app)
    {
        glDisable(GL_DEPTH_TEST);
        glUseProgram(app.backgroundProgram);
        glBindVertexArray(app.backgroundVao);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glBindVertexArray(0);
        glEnable(GL_DEPTH_TEST);
    }

    void CreateCube(AppState& app)
    {
        const float s = 0.5f;
        struct Vertex
        {
            float px, py, pz;
            float nx, ny, nz;
        };
        const Vertex vertices[] = {
            {-s, -s, s, 0, 0, 1},
            {s, -s, s, 0, 0, 1},
            {s, s, s, 0, 0, 1},
            {-s, s, s, 0, 0, 1},

            {s, -s, -s, 0, 0, -1},
            {-s, -s, -s, 0, 0, -1},
            {-s, s, -s, 0, 0, -1},
            {s, s, -s, 0, 0, -1},

            {-s, -s, -s, -1, 0, 0},
            {-s, -s, s, -1, 0, 0},
            {-s, s, s, -1, 0, 0},
            {-s, s, -s, -1, 0, 0},

            {s, -s, s, 1, 0, 0},
            {s, -s, -s, 1, 0, 0},
            {s, s, -s, 1, 0, 0},
            {s, s, s, 1, 0, 0},

            {-s, s, s, 0, 1, 0},
            {s, s, s, 0, 1, 0},
            {s, s, -s, 0, 1, 0},
            {-s, s, -s, 0, 1, 0},

            {-s, -s, -s, 0, -1, 0},
            {s, -s, -s, 0, -1, 0},
            {s, -s, s, 0, -1, 0},
            {-s, -s, s, 0, -1, 0}};

        const uint16_t indices[] = {
            0, 1, 2, 0, 2, 3,
            4, 5, 6, 4, 6, 7,
            8, 9, 10, 8, 10, 11,
            12, 13, 14, 12, 14, 15,
            16, 17, 18, 16, 18, 19,
            20, 21, 22, 20, 22, 23};

        glGenVertexArrays(1, &app.cubeVao);
        glGenBuffers(1, &app.cubeVbo);
        glGenBuffers(1, &app.cubeEbo);

        glBindVertexArray(app.cubeVao);
        glBindBuffer(GL_ARRAY_BUFFER, app.cubeVbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, app.cubeEbo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(0));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(sizeof(float) * 3));
        glBindVertexArray(0);

        const char* vsSource =
            "#version 300 es\n"
            "layout(location = 0) in vec3 aPos;\n"
            "layout(location = 1) in vec3 aNormal;\n"
            "uniform mat4 uMVP;\n"
            "uniform mat4 uModel;\n"
            "out vec3 vNormal;\n"
            "void main() {\n"
            "    vNormal = mat3(uModel) * aNormal;\n"
            "    gl_Position = uMVP * vec4(aPos, 1.0);\n"
            "}\n";

        const char* fsSource =
            "#version 300 es\n"
            "precision mediump float;\n"
            "in vec3 vNormal;\n"
            "uniform vec3 uColor;\n"
            "out vec4 FragColor;\n"
            "void main() {\n"
            "    vec3 lightDir = normalize(vec3(0.4, 0.8, 0.6));\n"
            "    float diff = max(dot(normalize(vNormal), lightDir), 0.15);\n"
            "    vec3 color = uColor * diff;\n"
            "    FragColor = vec4(color, 1.0);\n"
            "}\n";

        GLuint vs = CompileShader(GL_VERTEX_SHADER, vsSource);
        GLuint fs = CompileShader(GL_FRAGMENT_SHADER, fsSource);
        app.litProgram = LinkProgram(vs, fs);
        glDeleteShader(vs);
        glDeleteShader(fs);
    }

    void CreateGrid(AppState& app, int halfSize, float cellSize)
    {
        std::vector<float> vertices;
        const float extent = static_cast<float>(halfSize) * cellSize;
        for (int i = -halfSize; i <= halfSize; ++i)
        {
            const float position = static_cast<float>(i) * cellSize;
            vertices.push_back(-extent);
            vertices.push_back(0.0f);
            vertices.push_back(position);
            vertices.push_back(extent);
            vertices.push_back(0.0f);
            vertices.push_back(position);

            vertices.push_back(position);
            vertices.push_back(0.0f);
            vertices.push_back(-extent);
            vertices.push_back(position);
            vertices.push_back(0.0f);
            vertices.push_back(extent);
        }

        glGenVertexArrays(1, &app.gridVao);
        glGenBuffers(1, &app.gridVbo);
        glBindVertexArray(app.gridVao);
        glBindBuffer(GL_ARRAY_BUFFER, app.gridVbo);
        glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), vertices.data(), GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(float) * 3, nullptr);
        glBindVertexArray(0);

        const char* vsSource =
            "#version 300 es\n"
            "layout(location = 0) in vec3 aPos;\n"
            "uniform mat4 uMVP;\n"
            "void main() {\n"
            "    gl_Position = uMVP * vec4(aPos, 1.0);\n"
            "}\n";

        const char* fsSource =
            "#version 300 es\n"
            "precision mediump float;\n"
            "uniform vec3 uColor;\n"
            "out vec4 FragColor;\n"
            "void main() {\n"
            "    FragColor = vec4(uColor, 1.0);\n"
            "}\n";

        GLuint vs = CompileShader(GL_VERTEX_SHADER, vsSource);
        GLuint fs = CompileShader(GL_FRAGMENT_SHADER, fsSource);
        app.gridProgram = LinkProgram(vs, fs);
        glDeleteShader(vs);
        glDeleteShader(fs);
    }

    void CreateGlowGeometry(AppState& app)
    {
        constexpr int kSegments = 32;
        constexpr float radius = 1.8f;

        app.glowFanVertexCount = kSegments + 2;
        std::vector<float> vertices;
        vertices.reserve((app.glowFanVertexCount * 3) * 4);

        auto addFan = [&](int axis) {
            // Center vertex with full alpha.
            vertices.push_back(0.0f);
            vertices.push_back(0.0f);
            vertices.push_back(0.0f);
            vertices.push_back(0.45f);
            for (int i = 0; i <= kSegments; ++i)
            {
                const float theta = (static_cast<float>(i) / static_cast<float>(kSegments)) * 2.0f * 3.1415926535f;
                const float c = std::cos(theta) * radius;
                const float s = std::sin(theta) * radius;
                float x = 0.0f;
                float y = 0.0f;
                float z = 0.0f;
                if (axis == 0) // XY plane
                {
                    x = c;
                    y = s;
                }
                else if (axis == 1) // XZ plane
                {
                    x = c;
                    z = s;
                }
                else // YZ plane
                {
                    y = c;
                    z = s;
                }
                vertices.push_back(x);
                vertices.push_back(y);
                vertices.push_back(z);
                vertices.push_back(0.0f);
            }
        };

        addFan(0);
        addFan(1);
        addFan(2);

        glGenVertexArrays(1, &app.glowVao);
        glGenBuffers(1, &app.glowVbo);
        glBindVertexArray(app.glowVao);
        glBindBuffer(GL_ARRAY_BUFFER, app.glowVbo);
        glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), vertices.data(), GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(float) * 4, reinterpret_cast<void*>(0));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, sizeof(float) * 4, reinterpret_cast<void*>(sizeof(float) * 3));
        glBindVertexArray(0);

        const char* vsSource =
            "#version 300 es\n"
            "layout(location = 0) in vec3 aPos;\n"
            "layout(location = 1) in float aAlpha;\n"
            "uniform mat4 uMVP;\n"
            "out float vAlpha;\n"
            "void main() {\n"
            "    vAlpha = aAlpha;\n"
            "    gl_Position = uMVP * vec4(aPos, 1.0);\n"
            "}\n";

        const char* fsSource =
            "#version 300 es\n"
            "precision mediump float;\n"
            "in float vAlpha;\n"
            "uniform vec3 uColor;\n"
            "out vec4 FragColor;\n"
            "void main() {\n"
            "    FragColor = vec4(uColor * vAlpha, vAlpha);\n"
            "}\n";

        GLuint vs = CompileShader(GL_VERTEX_SHADER, vsSource);
        GLuint fs = CompileShader(GL_FRAGMENT_SHADER, fsSource);
        app.glowProgram = LinkProgram(vs, fs);
        glDeleteShader(vs);
        glDeleteShader(fs);
    }

    void RenderGlowEffects(AppState& app, const Mat4& vp)
    {
        if (app.glowProgram == 0 || app.glowVao == 0 || app.glowFanVertexCount == 0)
        {
            return;
        }

        const GLsizei fanCount = app.glowFanVertexCount;
        const GLint offsets[3] = {0, fanCount, fanCount * 2};

        glUseProgram(app.glowProgram);
        GLint mvpLoc = glGetUniformLocation(app.glowProgram, "uMVP");
        GLint colorLoc = glGetUniformLocation(app.glowProgram, "uColor");

        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE);
        glDepthMask(GL_FALSE);
        glDisable(GL_CULL_FACE);

        glBindVertexArray(app.glowVao);

        for (const PlacedCube& cube : app.cubes)
        {
            if (!cube.glowing)
            {
                continue;
            }
            Mat4 model = Mat4::Identity();
            model.m[12] = static_cast<float>(cube.gridX);
            model.m[13] = 0.5f;
            model.m[14] = static_cast<float>(cube.gridZ);
            Mat4 mvp = Multiply(vp, model);
            glUniformMatrix4fv(mvpLoc, 1, GL_FALSE, mvp.m);
            glUniform3f(colorLoc, cube.r, cube.g, cube.b);
            for (int i = 0; i < 3; ++i)
            {
                glDrawArrays(GL_TRIANGLE_FAN, offsets[i], fanCount);
            }
        }

        glBindVertexArray(0);
        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);
        glEnable(GL_CULL_FACE);
        glUseProgram(0);
    }

    void EnsureRaytraceResources(AppState& app)
    {
        if (app.raytraceTexture == 0)
        {
            glGenTextures(1, &app.raytraceTexture);
            glBindTexture(GL_TEXTURE_2D, app.raytraceTexture);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, app.raytraceWidth, app.raytraceHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glBindTexture(GL_TEXTURE_2D, 0);
        }

        if (app.raytraceFbo == 0)
        {
            glGenFramebuffers(1, &app.raytraceFbo);
            glBindFramebuffer(GL_FRAMEBUFFER, app.raytraceFbo);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, app.raytraceTexture, 0);
            GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
            if (status != GL_FRAMEBUFFER_COMPLETE)
            {
                SDL_Log("Raytrace framebuffer incomplete: 0x%x", status);
            }
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
        }

        if (app.raytraceProgram == 0)
        {
            const char* vsSource =
                "#version 300 es\n"
                "layout(location = 0) in vec2 aPos;\n"
                "void main() {\n"
                "    gl_Position = vec4(aPos, 0.0, 1.0);\n"
                "}\n";

            const char* fsSource =
                "#version 300 es\n"
                "precision highp float;\n"
                "#define MAX_RAYTRACE_CUBES 64\n"
                "uniform vec2 uResolution;\n"
                "uniform vec3 uCameraPos;\n"
                "uniform vec3 uCameraDir;\n"
                "uniform vec3 uCameraRight;\n"
                "uniform vec3 uCameraUp;\n"
                "uniform float uFovTan;\n"
                "uniform int uCubeCount;\n"
                "uniform vec4 uCubeData[MAX_RAYTRACE_CUBES];\n"
                "uniform vec4 uCubeColor[MAX_RAYTRACE_CUBES];\n"
                "uniform int uCubeGlow[MAX_RAYTRACE_CUBES];\n"
                "out vec4 FragColor;\n"
                "\n"
                "bool IntersectCube(vec3 ro, vec3 rd, vec3 center, out float tHit, out vec3 normal)\n"
                "{\n"
                "    vec3 minB = center + vec3(-0.5, 0.0, -0.5);\n"
                "    vec3 maxB = center + vec3(0.5, 1.0, 0.5);\n"
                "    vec3 invDir = 1.0 / rd;\n"
                "    vec3 t0 = (minB - ro) * invDir;\n"
                "    vec3 t1 = (maxB - ro) * invDir;\n"
                "    vec3 tmin = min(t0, t1);\n"
                "    vec3 tmax = max(t0, t1);\n"
                "    float tNear = max(max(tmin.x, tmin.y), tmin.z);\n"
                "    float tFar = min(min(tmax.x, tmax.y), tmax.z);\n"
                "    if (tNear > tFar || tFar < 0.0)\n"
                "        return false;\n"
                "    tHit = max(tNear, 0.0);\n"
                "    vec3 hit = ro + rd * tHit;\n"
                "    const float eps = 0.001;\n"
                "    if (abs(hit.x - minB.x) < eps) normal = vec3(-1.0, 0.0, 0.0);\n"
                "    else if (abs(hit.x - maxB.x) < eps) normal = vec3(1.0, 0.0, 0.0);\n"
                "    else if (abs(hit.y - minB.y) < eps) normal = vec3(0.0, -1.0, 0.0);\n"
                "    else if (abs(hit.y - maxB.y) < eps) normal = vec3(0.0, 1.0, 0.0);\n"
                "    else if (abs(hit.z - minB.z) < eps) normal = vec3(0.0, 0.0, -1.0);\n"
                "    else normal = vec3(0.0, 0.0, 1.0);\n"
                "    return true;\n"
                "}\n"
                "\n"
                "bool Occluded(vec3 origin, vec3 dir, float maxT, int ignoreIndex)\n"
                "{\n"
                "    float tTmp;\n"
                "    vec3 nTmp;\n"
                "    for (int i = 0; i < uCubeCount; ++i)\n"
                "    {\n"
                "        if (i == ignoreIndex)\n"
                "            continue;\n"
                "        if (IntersectCube(origin, dir, uCubeData[i].xyz, tTmp, nTmp))\n"
                "        {\n"
                "            if (tTmp > 0.02 && tTmp < maxT - 0.02)\n"
                "                return true;\n"
                "        }\n"
                "    }\n"
                "    return false;\n"
                "}\n"
                "\n"
                "vec3 Shade(vec3 pos, vec3 normal, vec3 baseColor, int selfIndex)\n"
                "{\n"
                "    vec3 result = baseColor * 0.15;\n"
                "    for (int i = 0; i < uCubeCount; ++i)\n"
                "    {\n"
                "        if (uCubeGlow[i] == 0)\n"
                "            continue;\n"
                "        vec3 lightPos = uCubeData[i].xyz;\n"
                "        vec3 L = lightPos - pos;\n"
                "        float dist = length(L);\n"
                "        if (dist < 0.0001)\n"
                "            continue;\n"
                "        L /= dist;\n"
                "        if (Occluded(pos + normal * 0.02, L, dist, i))\n"
                "            continue;\n"
                "        float diff = max(dot(normal, L), 0.0);\n"
                "        float attenuation = 1.0 / (1.0 + dist * 0.6 + dist * dist * 0.15);\n"
                "        result += baseColor * diff * attenuation * 2.0;\n"
                "        if (selfIndex == i)\n"
                "        {\n"
                "            result += uCubeColor[i].rgb * 0.8;\n"
                "        }\n"
                "    }\n"
                "    return clamp(result, 0.0, 1.0);\n"
                "}\n"
                "\n"
                "vec3 ShadeGround(vec3 pos)\n"
                "{\n"
                "    vec3 base = mix(vec3(0.08, 0.08, 0.09), vec3(0.28, 0.29, 0.32), 0.4 + 0.6 * clamp((pos.y + 0.01) * 10.0, 0.0, 1.0));\n"
                "    vec3 normal = vec3(0.0, 1.0, 0.0);\n"
                "    vec3 shaded = base * 0.15;\n"
                "    for (int i = 0; i < uCubeCount; ++i)\n"
                "    {\n"
                "        if (uCubeGlow[i] == 0)\n"
                "            continue;\n"
                "        vec3 lightPos = uCubeData[i].xyz;\n"
                "        vec3 L = lightPos - pos;\n"
                "        float dist = length(L);\n"
                "        if (dist < 0.0001)\n"
                "            continue;\n"
                "        L /= dist;\n"
                "        if (Occluded(pos + normal * 0.02, L, dist, i))\n"
                "            continue;\n"
                "        float diff = max(dot(normal, L), 0.0);\n"
                "        float attenuation = 1.0 / (1.0 + dist * 0.6 + dist * dist * 0.15);\n"
                "        shaded += vec3(0.9, 0.9, 1.0) * diff * attenuation * 1.5;\n"
                "    }\n"
                "    return clamp(shaded, 0.0, 1.0);\n"
                "}\n"
                "\n"
                "void main()\n"
                "{\n"
                "    vec2 uv = (gl_FragCoord.xy / uResolution) * 2.0 - 1.0;\n"
                "    uv.x *= uResolution.x / uResolution.y;\n"
                "    vec3 rd = normalize(uCameraDir + uv.x * uCameraRight * uFovTan + uv.y * uCameraUp * uFovTan);\n"
                "    vec3 ro = uCameraPos;\n"
                "\n"
                "    float closestT = 1e9;\n"
                "    vec3 hitColor = vec3(0.0);\n"
                "    bool hitSomething = false;\n"
                "\n"
                "    float tCube;\n"
                "    vec3 normalCube;\n"
                "    int hitIndex = -1;\n"
                "    for (int i = 0; i < uCubeCount; ++i)\n"
                "    {\n"
                "        if (IntersectCube(ro, rd, uCubeData[i].xyz, tCube, normalCube))\n"
                "        {\n"
                "            if (tCube < closestT)\n"
                "            {\n"
                "                closestT = tCube;\n"
                "                vec3 pos = ro + rd * tCube;\n"
                "                hitColor = Shade(pos, normalCube, uCubeColor[i].rgb, i);\n"
                "                hitSomething = true;\n"
                "                hitIndex = i;\n"
                "            }\n"
                "        }\n"
                "    }\n"
                "\n"
                "    vec3 groundNormal = vec3(0.0, 1.0, 0.0);\n"
                "    if (abs(rd.y) > 1e-4)\n"
                "    {\n"
                "        float tGround = (-ro.y) / rd.y;\n"
                "        if (tGround > 0.0 && tGround < closestT)\n"
                "        {\n"
                "            vec3 pos = ro + rd * tGround;\n"
                "            hitColor = ShadeGround(pos);\n"
                "            hitSomething = true;\n"
                "        }\n"
                "    }\n"
                "\n"
                "    if (!hitSomething)\n"
                "    {\n"
                "        vec3 top = vec3(0.18, 0.13, 0.25);\n"
                "        vec3 bottom = vec3(0.03, 0.05, 0.12);\n"
                "        float t = clamp(uv.y * 0.5 + 0.5, 0.0, 1.0);\n"
                "        hitColor = mix(bottom, top, t);\n"
                "    }\n"
                "\n"
                "    FragColor = vec4(hitColor, 1.0);\n"
                "}\n";

            GLuint vs = CompileShader(GL_VERTEX_SHADER, vsSource);
            GLuint fs = CompileShader(GL_FRAGMENT_SHADER, fsSource);
            app.raytraceProgram = LinkProgram(vs, fs);
            glDeleteShader(vs);
            glDeleteShader(fs);
            if (app.raytraceProgram)
            {
                app.raytraceLocResolution = glGetUniformLocation(app.raytraceProgram, "uResolution");
                app.raytraceLocCameraPos = glGetUniformLocation(app.raytraceProgram, "uCameraPos");
                app.raytraceLocCameraDir = glGetUniformLocation(app.raytraceProgram, "uCameraDir");
                app.raytraceLocCameraRight = glGetUniformLocation(app.raytraceProgram, "uCameraRight");
                app.raytraceLocCameraUp = glGetUniformLocation(app.raytraceProgram, "uCameraUp");
                app.raytraceLocFovTan = glGetUniformLocation(app.raytraceProgram, "uFovTan");
                app.raytraceLocCubeCount = glGetUniformLocation(app.raytraceProgram, "uCubeCount");
                app.raytraceLocCubeData = glGetUniformLocation(app.raytraceProgram, "uCubeData");
                app.raytraceLocCubeColor = glGetUniformLocation(app.raytraceProgram, "uCubeColor");
                app.raytraceLocCubeGlow = glGetUniformLocation(app.raytraceProgram, "uCubeGlow");
            }
        }
    }

    void CompileRaytracedScene(AppState& app)
    {
        EnsureRaytraceResources(app);
        if (app.raytraceProgram == 0 || app.raytraceFbo == 0)
        {
            return;
        }

        std::array<float, kMaxRaytraceCubes * 4> cubeData{};
        std::array<float, kMaxRaytraceCubes * 4> cubeColors{};
        std::array<int, kMaxRaytraceCubes> cubeGlow{};

        int cubeCount = 0;
        for (const PlacedCube& cube : app.cubes)
        {
            if (cubeCount >= kMaxRaytraceCubes)
            {
                break;
            }
            const int index = cubeCount * 4;
            cubeData[index + 0] = static_cast<float>(cube.gridX);
            cubeData[index + 1] = 0.5f;
            cubeData[index + 2] = static_cast<float>(cube.gridZ);
            cubeData[index + 3] = 1.0f;

            cubeColors[index + 0] = cube.r;
            cubeColors[index + 1] = cube.g;
            cubeColors[index + 2] = cube.b;
            cubeColors[index + 3] = 1.0f;

            cubeGlow[cubeCount] = cube.glowing ? 1 : 0;
            ++cubeCount;
        }

        GLint previousFbo = 0;
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &previousFbo);
        GLint previousViewport[4];
        glGetIntegerv(GL_VIEWPORT, previousViewport);

        glBindFramebuffer(GL_FRAMEBUFFER, app.raytraceFbo);
        glViewport(0, 0, app.raytraceWidth, app.raytraceHeight);
        glUseProgram(app.raytraceProgram);

        if (app.raytraceLocResolution >= 0)
        {
            glUniform2f(app.raytraceLocResolution, static_cast<float>(app.raytraceWidth), static_cast<float>(app.raytraceHeight));
        }
        if (app.raytraceLocCameraPos >= 0)
        {
            glUniform3f(app.raytraceLocCameraPos, app.cameraEye.x, app.cameraEye.y, app.cameraEye.z);
        }
        if (app.raytraceLocCameraDir >= 0)
        {
            glUniform3f(app.raytraceLocCameraDir, app.cameraDir.x, app.cameraDir.y, app.cameraDir.z);
        }
        if (app.raytraceLocCameraRight >= 0)
        {
            glUniform3f(app.raytraceLocCameraRight, app.cameraRightVec.x, app.cameraRightVec.y, app.cameraRightVec.z);
        }
        if (app.raytraceLocCameraUp >= 0)
        {
            glUniform3f(app.raytraceLocCameraUp, app.cameraUpVec.x, app.cameraUpVec.y, app.cameraUpVec.z);
        }
        if (app.raytraceLocFovTan >= 0)
        {
            float tanHalfFov = std::tan(60.0f * (3.1415926535f / 180.0f) * 0.5f);
            glUniform1f(app.raytraceLocFovTan, tanHalfFov);
        }
        if (app.raytraceLocCubeCount >= 0)
        {
            glUniform1i(app.raytraceLocCubeCount, cubeCount);
        }
        if (cubeCount > 0)
        {
            if (app.raytraceLocCubeData >= 0)
            {
                glUniform4fv(app.raytraceLocCubeData, cubeCount, cubeData.data());
            }
            if (app.raytraceLocCubeColor >= 0)
            {
                glUniform4fv(app.raytraceLocCubeColor, cubeCount, cubeColors.data());
            }
            if (app.raytraceLocCubeGlow >= 0)
            {
                glUniform1iv(app.raytraceLocCubeGlow, cubeCount, cubeGlow.data());
            }
        }

        glBindVertexArray(app.backgroundVao);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glBindVertexArray(0);

        glBindFramebuffer(GL_FRAMEBUFFER, previousFbo);
        glViewport(previousViewport[0], previousViewport[1], previousViewport[2], previousViewport[3]);
        glUseProgram(0);

        app.showRaytrace = true;
    }

    bool HandleEvents(AppState& app)
    {
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT)
            {
                return false;
            }
            if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_RESIZED)
            {
                app.windowWidth = event.window.data1;
                app.windowHeight = event.window.data2;
                glViewport(0, 0, app.windowWidth, app.windowHeight);
            }
            if (event.type == SDL_MOUSEWHEEL)
            {
                if (!ImGui::GetIO().WantCaptureMouse)
                {
                    app.cameraDistance -= static_cast<float>(event.wheel.y) * 0.6f;
                    app.cameraDistance = std::clamp(app.cameraDistance, 4.0f, 18.0f);
                }
            }
            if (event.type == SDL_MOUSEBUTTONDOWN)
            {
                if (!ImGui::GetIO().WantCaptureMouse)
                {
                    const int mouseX = event.button.x;
                    const int mouseY = event.button.y;

                    const Mat4 vp = Multiply(app.projection, app.view);
                    Mat4 invVP{};
                    if (!Invert(vp, invVP))
                    {
                        continue;
                    }

                    const float ndcX = (static_cast<float>(mouseX) / static_cast<float>(app.windowWidth) * 2.0f) - 1.0f;
                    const float ndcY = 1.0f - (static_cast<float>(mouseY) / static_cast<float>(app.windowHeight) * 2.0f);

                    Vec3 nearPoint = TransformPoint(invVP, Vec3{ndcX, ndcY, -1.0f});
                    Vec3 farPoint = TransformPoint(invVP, Vec3{ndcX, ndcY, 1.0f});

                    Vec3 direction = farPoint - nearPoint;
                    if (std::fabs(direction.y) < 1e-4f)
                    {
                        continue;
                    }

                    const float t = -nearPoint.y / direction.y;
                    if (t < 0.0f)
                    {
                        continue;
                    }

                    Vec3 hitPoint = nearPoint + direction * t;
                    const int gridX = static_cast<int>(std::round(hitPoint.x));
                    const int gridZ = static_cast<int>(std::round(hitPoint.z));

                    if (std::abs(gridX) > 10 || std::abs(gridZ) > 10)
                    {
                        continue;
                    }

                    auto it = std::find_if(app.cubes.begin(), app.cubes.end(), [gridX, gridZ](const PlacedCube& cube) {
                        return cube.gridX == gridX && cube.gridZ == gridZ;
                    });

                    if (event.button.button == SDL_BUTTON_LEFT)
                    {
                        const SpawnPreset preset = GetPreset(app.selectedPreset);
                        if (it == app.cubes.end())
                        {
                            app.cubes.push_back({gridX, gridZ, preset.r, preset.g, preset.b, preset.glowing});
                        }
                        else
                        {
                            it->r = preset.r;
                            it->g = preset.g;
                            it->b = preset.b;
                            it->glowing = preset.glowing;
                        }
                    }
                    else if (event.button.button == SDL_BUTTON_RIGHT)
                    {
                        if (it != app.cubes.end())
                        {
                            app.cubes.erase(it);
                        }
                    }
                }
            }
        }
        return true;
    }

    void UpdateCamera(AppState& app)
    {
        float moveX = 0.0f;
        float moveZ = 0.0f;
        if (app.cameraForward)
        {
            moveZ -= 1.0f;
        }
        if (app.cameraBackward)
        {
            moveZ += 1.0f;
        }
        if (app.cameraLeft)
        {
            moveX -= 1.0f;
        }
        if (app.cameraRight)
        {
            moveX += 1.0f;
        }
        if (std::fabs(moveX) > 0.0f || std::fabs(moveZ) > 0.0f)
        {
            const float len = std::sqrt(moveX * moveX + moveZ * moveZ);
            moveX /= len;
            moveZ /= len;
            app.cameraFocus.x += moveX * app.deltaTime * 6.0f;
            app.cameraFocus.z += moveZ * app.deltaTime * 6.0f;
        }

        const float pitchRadians = 65.0f * (3.1415926535f / 180.0f);
        const float cameraHeight = std::sin(pitchRadians) * app.cameraDistance;
        const float cameraForward = std::cos(pitchRadians) * app.cameraDistance;
        const Vec3 eye{app.cameraFocus.x, cameraHeight, app.cameraFocus.z + cameraForward};
        const Vec3 target{app.cameraFocus.x, 0.0f, app.cameraFocus.z};
        const Vec3 forward = Vec3::Normalize(target - eye);
        const Vec3 right = Vec3::Normalize(Vec3::Cross(forward, Vec3{0.0f, 1.0f, 0.0f}));
        const Vec3 up = Vec3::Cross(right, forward * -1.0f);
        app.cameraEye = eye;
        app.cameraDir = forward;
        app.cameraRightVec = right;
        app.cameraUpVec = up;
        app.view = LookAt(eye, target, Vec3{0.0f, 1.0f, 0.0f});
        const float aspect = static_cast<float>(app.windowWidth) / static_cast<float>(app.windowHeight);
        app.projection = Perspective(60.0f * (3.1415926535f / 180.0f), aspect, 0.1f, 100.0f);
    }

    void RenderScene(AppState& app)
    {
        glEnable(GL_DEPTH_TEST);
        glCullFace(GL_BACK);
        glEnable(GL_CULL_FACE);

        RenderGradientBackground(app);

        const Mat4 vp = Multiply(app.projection, app.view);

        glUseProgram(app.gridProgram);
        GLint mvpLoc = glGetUniformLocation(app.gridProgram, "uMVP");
        GLint colorLoc = glGetUniformLocation(app.gridProgram, "uColor");
        glUniformMatrix4fv(mvpLoc, 1, GL_FALSE, vp.m);
        glUniform3f(colorLoc, 0.35f, 0.35f, 0.4f);
        glBindVertexArray(app.gridVao);
        glDrawArrays(GL_LINES, 0, (10 * 2 + 2) * 2);
        glBindVertexArray(0);

        glUseProgram(app.litProgram);
        GLint litMvpLoc = glGetUniformLocation(app.litProgram, "uMVP");
        GLint litModelLoc = glGetUniformLocation(app.litProgram, "uModel");
        GLint litColorLoc = glGetUniformLocation(app.litProgram, "uColor");

        auto renderCubeAt = [&](float x, float y, float z, const Vec3& color) {
            Mat4 model = Mat4::Identity();
            model.m[12] = x;
            model.m[13] = y;
            model.m[14] = z;
            Mat4 mvp = Multiply(vp, model);
            glUniformMatrix4fv(litMvpLoc, 1, GL_FALSE, mvp.m);
            glUniformMatrix4fv(litModelLoc, 1, GL_FALSE, model.m);
            glUniform3f(litColorLoc, color.x, color.y, color.z);
            glBindVertexArray(app.cubeVao);
            glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_SHORT, nullptr);
            glBindVertexArray(0);
        };

        for (const PlacedCube& cube : app.cubes)
        {
            renderCubeAt(static_cast<float>(cube.gridX), 0.5f, static_cast<float>(cube.gridZ), Vec3{cube.r, cube.g, cube.b});
        }

        renderCubeAt(app.cameraFocus.x, 0.5f, app.cameraFocus.z, Vec3{0.6f, 0.7f, 1.0f});

        RenderGlowEffects(app, vp);
    }

    const std::unordered_set<std::string> kLuaKeywords = {
        "and", "break", "do", "else", "elseif", "end", "false", "for",
        "function", "goto", "if", "in", "local", "nil", "not", "or",
        "repeat", "return", "then", "true", "until", "while"};

    bool IsIdentifierChar(char c)
    {
        return static_cast<bool>(std::isalnum(static_cast<unsigned char>(c)) || c == '_');
    }

    void RenderLuaToken(ImDrawList* drawList, const char* begin, const char* end, ImVec2& cursor, ImU32 color)
    {
        if (begin >= end)
        {
            return;
        }
        drawList->AddText(cursor, color, begin, end);
        const ImVec2 size = ImGui::CalcTextSize(begin, end, false);
        cursor.x += size.x;
    }

    void RenderLuaLineTokens(ImDrawList* drawList,
                             const char* lineStart,
                             const char* lineEnd,
                             ImVec2& cursor,
                             ImU32 defaultColor,
                             ImU32 keywordColor,
                             ImU32 stringColor,
                             ImU32 commentColor,
                             ImU32 numberColor)
    {
        const char* ptr = lineStart;
        while (ptr < lineEnd)
        {
            if (*ptr == '\r')
            {
                ++ptr;
                continue;
            }
            if (*ptr == '-' && (ptr + 1) < lineEnd && *(ptr + 1) == '-')
            {
                RenderLuaToken(drawList, ptr, lineEnd, cursor, commentColor);
                break;
            }
            if (*ptr == '"' || *ptr == '\'')
            {
                const char quote = *ptr;
                const char* start = ptr++;
                while (ptr < lineEnd)
                {
                    if (*ptr == '\\' && (ptr + 1) < lineEnd)
                    {
                        ptr += 2;
                        continue;
                    }
                    if (*ptr == quote)
                    {
                        ++ptr;
                        break;
                    }
                    ++ptr;
                }
                RenderLuaToken(drawList, start, ptr, cursor, stringColor);
                continue;
            }
            if (std::isdigit(static_cast<unsigned char>(*ptr)))
            {
                const char* start = ptr++;
                while (ptr < lineEnd && (std::isdigit(static_cast<unsigned char>(*ptr)) || *ptr == '.' || *ptr == 'x' || *ptr == 'X'))
                {
                    ++ptr;
                }
                RenderLuaToken(drawList, start, ptr, cursor, numberColor);
                continue;
            }
            if (IsIdentifierChar(*ptr))
            {
                const char* start = ptr++;
                while (ptr < lineEnd && IsIdentifierChar(*ptr))
                {
                    ++ptr;
                }
                std::string identifier(start, ptr);
                const bool isKeyword = kLuaKeywords.find(identifier) != kLuaKeywords.end();
                RenderLuaToken(drawList, start, ptr, cursor, isKeyword ? keywordColor : defaultColor);
                continue;
            }
            RenderLuaToken(drawList, ptr, ptr + 1, cursor, defaultColor);
            ++ptr;
        }
    }

    void RenderLuaHighlightedText(const std::string& text, const ImVec2& origin, const ImVec2& size)
    {
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        ImGuiStyle& style = ImGui::GetStyle();
        ImVec2 cursor = ImVec2(origin.x + style.FramePadding.x, origin.y + style.FramePadding.y);
        const float lineHeight = ImGui::GetTextLineHeight();
        const ImU32 defaultColor = ImGui::GetColorU32(ImGui::GetStyleColorVec4(ImGuiCol_Text));
        const ImU32 keywordColor = ImGui::GetColorU32(ImVec4(0.70f, 0.55f, 1.0f, 1.0f));
        const ImU32 stringColor = ImGui::GetColorU32(ImVec4(0.90f, 0.80f, 0.45f, 1.0f));
        const ImU32 commentColor = ImGui::GetColorU32(ImVec4(0.45f, 0.80f, 0.45f, 1.0f));
        const ImU32 numberColor = ImGui::GetColorU32(ImVec4(0.90f, 0.60f, 0.45f, 1.0f));

        const ImVec2 clipMax = ImVec2(origin.x + size.x, origin.y + size.y);
        drawList->PushClipRect(origin, clipMax, true);

        const char* ptr = text.c_str();
        while (*ptr)
        {
            const char* lineStart = ptr;
            while (*ptr && *ptr != '\n')
            {
                ++ptr;
            }
            const char* lineEnd = ptr;
            ImVec2 lineCursor = cursor;
            RenderLuaLineTokens(drawList, lineStart, lineEnd, lineCursor, defaultColor, keywordColor, stringColor, commentColor, numberColor);
            cursor.y += lineHeight;
            if (*ptr == '\n')
            {
                ++ptr;
            }
            else
            {
                break;
            }
        }

        drawList->PopClipRect();
    }

    void UpdateImGui(AppState& app)
    {
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        ImGuiWindowFlags overlayFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                                        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings;
        ImGui::SetNextWindowPos(ImVec2(10.0f, 10.0f), ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.6f);
        ImGui::Begin("CodeOverlay", nullptr, overlayFlags);
        const char* toggleLabel = app.showDocs ? "Show Code" : (app.codeDirty ? "Hide Code*" : "Hide Code");
        if (ImGui::Button(app.showDocs ? (app.codeDirty ? "Show Code*" : "Show Code") : (app.codeDirty ? "Hide Code*" : "Hide Code")))
        {
            app.showDocs = false;
            app.showContent = false;
            app.codeDirty = app.codeDirty;
        }
        ImGui::SameLine();
        if (ImGui::Button(app.showDocs ? "Docs On" : "Docs"))
        {
            app.showDocs = !app.showDocs;
        }
        ImGui::SameLine();
        if (ImGui::Button(app.showContent ? "Content On" : "Content"))
        {
            app.showContent = !app.showContent;
        }
        if (app.codeDirty)
        {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.2f, 1.0f), "unsaved");
        }
        ImGui::SameLine();
        if (ImGui::Button("Compile Scene"))
        {
            CompileRaytracedScene(app);
        }
        ImGui::End();

        const float panelWidth = std::max(360.0f, static_cast<float>(app.windowWidth) * 0.38f);
        const float panelHeight = std::max(240.0f, static_cast<float>(app.windowHeight) * 0.65f);
        const bool showCodePanel = !app.showDocs;
        const bool anyPanelVisible = app.showDocs || showCodePanel;
        if (anyPanelVisible)
        {
            ImGui::SetNextWindowPos(ImVec2(20.0f, 60.0f), ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(panelWidth, panelHeight), ImGuiCond_Always);
            ImGuiWindowFlags codeFlags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings;
            ImGui::Begin("CodePanel", nullptr, codeFlags);

            if (app.showDocs)
            {
                ImGui::TextUnformatted("Documentation");
                ImGui::Separator();
                if (ImGui::Button("Copy##DocsPanel"))
                {
                    ImGui::SetClipboardText(app.docsContent.c_str());
                }
                ImVec2 size = ImGui::GetContentRegionAvail();
                if (size.y < 120.0f)
                {
                    size.y = 120.0f;
                }
                ImGui::InputTextMultiline("##DocsContent", &app.docsContent, size,
                                          ImGuiInputTextFlags_ReadOnly | ImGuiInputTextFlags_AllowTabInput | ImGuiInputTextFlags_NoUndoRedo);
            }
            else
            {
                ImGui::TextUnformatted("Code");
                ImGui::Separator();
                if (ImGui::Button("Save##CodePanel"))
                {
                    app.codeDirty = false;
                }
                ImGui::SameLine();
                ImGui::TextUnformatted(app.codeDirty ? "Modified" : "Saved");
                ImGui::Spacing();

                ImVec2 size = ImGui::GetContentRegionAvail();
                ImVec2 editorPos = ImGui::GetCursorScreenPos();
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0, 0, 0, 0));
                bool edited = ImGui::InputTextMultiline("##CodeContent",
                                                        &app.codeContent,
                                                        size,
                                                        ImGuiInputTextFlags_AllowTabInput | ImGuiInputTextFlags_CallbackCharFilter | ImGuiInputTextFlags_CallbackEdit,
                                                        [](ImGuiInputTextCallbackData* data) -> int {
                                                            constexpr const char* kTab = "    ";
                                                            if (data->EventFlag == ImGuiInputTextFlags_CallbackCharFilter)
                                                            {
                                                                if (data->EventChar == '\t')
                                                                {
                                                                    data->InsertChars(data->CursorPos, kTab);
                                                                    return 1;
                                                                }
                                                            }
                                                            else if (data->EventFlag == ImGuiInputTextFlags_CallbackEdit)
                                                            {
                                                                if (data->CursorPos > 0)
                                                                {
                                                                    const char* buf = data->Buf;
                                                                    const int pos = data->CursorPos;
                                                                    int lineStart = pos - 1;
                                                                    while (lineStart >= 0 && buf[lineStart] != '\n')
                                                                    {
                                                                        --lineStart;
                                                                    }
                                                                    ++lineStart;
                                                                    int indent = 0;
                                                                    int iter = lineStart;
                                                                    while (iter < pos && buf[iter] == ' ')
                                                                    {
                                                                        ++indent;
                                                                        ++iter;
                                                                    }
                                                                    bool endsWithFunction = false;
                                                                    for (int i = iter; i < pos; ++i)
                                                                    {
                                                                        if (!std::isspace(static_cast<unsigned char>(buf[i])))
                                                                        {
                                                                            const char* keyword = "function";
                                                                            const int len = 8;
                                                                            if (i + len <= data->BufTextLen && std::strncmp(&buf[i], keyword, len) == 0)
                                                                            {
                                                                                endsWithFunction = true;
                                                                            }
                                                                            break;
                                                                        }
                                                                    }
                                                                    if (pos > 0 && buf[pos - 1] == '\n')
                                                                    {
                                                                        std::string indentStr(indent, ' ');
                                                                        if (endsWithFunction)
                                                                        {
                                                                            indentStr += kTab;
                                                                        }
                                                                        data->InsertChars(data->CursorPos, indentStr.c_str());
                                                                    }
                                                                    else if (pos >= 3)
                                                                    {
                                                                        const char* endKeyword = "end";
                                                                        if (std::strncmp(&buf[pos - 3], endKeyword, 3) == 0)
                                                                        {
                                                                            int remove = std::min(indent, 4);
                                                                            int removeStart = pos - 3;
                                                                            while (remove > 0 && removeStart > 0 && buf[removeStart - 1] == ' ')
                                                                            {
                                                                                --removeStart;
                                                                                --remove;
                                                                            }
                                                                            if (removeStart < pos - 3)
                                                                            {
                                                                                data->DeleteChars(removeStart, (pos - 3) - removeStart);
                                                                                data->CursorPos = removeStart + 3;
                                                                            }
                                                                        }
                                                                    }
                                                                }
                                                            }
                                                            return 0;
                                                        });
                ImGui::PopStyleColor();
                if (edited)
                {
                    app.codeDirty = true;
                }
                RenderLuaHighlightedText(app.codeContent, editorPos, size);
            }

            ImGui::End();
        }

        const float contentTargetY = app.showContent ? static_cast<float>(app.windowHeight) - 200.0f : static_cast<float>(app.windowHeight) + 200.0f;
        const float slideAlpha = std::clamp(app.deltaTime * 12.0f, 0.0f, 1.0f);
        app.showContent ? (void)0 : (void)0;

        if (app.showContent)
        {
            ImGui::SetNextWindowPos(ImVec2(app.windowWidth * 0.5f - 300.0f, contentTargetY), ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(600.0f, 180.0f), ImGuiCond_Always);
            ImGui::Begin("ContentPanel", nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings);
            ImGui::TextUnformatted("Content Browser");
            ImGui::Separator();
            ImGui::TextWrapped("Select a preset and click on the grid to spawn it. Right click removes cubes.");
            ImGui::Spacing();
            for (int i = 0; i < static_cast<int>(std::size(kSpawnPresets)); ++i)
            {
                SpawnPreset preset = GetPreset(i);
                ImVec4 color(preset.r, preset.g, preset.b, 1.0f);
                ImGui::PushID(i);
                ImGui::ColorButton("##preview", color, ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoBorder, ImVec2(24.0f, 24.0f));
                ImGui::SameLine();
                if (ImGui::Selectable(preset.name, app.selectedPreset == i))
                {
                    app.selectedPreset = i;
                }
                if (preset.glowing)
                {
                    ImGui::SameLine();
                    ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f), "glow");
                }
                ImGui::PopID();
            }
            ImGui::End();
        }

        if (app.showRaytrace && app.raytraceTexture != 0)
        {
            ImGui::SetNextWindowSize(ImVec2(560.0f, 600.0f), ImGuiCond_Appearing);
            if (ImGui::Begin("Raytrace Preview", &app.showRaytrace, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings))
            {
                ImGui::TextUnformatted("Path-traced lighting preview");
                ImGui::Separator();
                ImVec2 avail = ImGui::GetContentRegionAvail();
                float aspect = static_cast<float>(app.raytraceHeight) > 0 ? static_cast<float>(app.raytraceWidth) / static_cast<float>(app.raytraceHeight) : 1.0f;
                ImVec2 imageSize = avail;
                if (imageSize.y > imageSize.x / aspect)
                {
                    imageSize.y = imageSize.x / aspect;
                }
                else
                {
                    imageSize.x = imageSize.y * aspect;
                }
                ImGui::Image(reinterpret_cast<ImTextureID>(static_cast<intptr_t>(app.raytraceTexture)), imageSize, ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f));
            }
            ImGui::End();
        }

        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    }

    void Cleanup(AppState& app)
    {
        if (app.litProgram)
            glDeleteProgram(app.litProgram);
        if (app.gridProgram)
            glDeleteProgram(app.gridProgram);
        if (app.backgroundProgram)
            glDeleteProgram(app.backgroundProgram);
        if (app.cubeVao)
            glDeleteVertexArrays(1, &app.cubeVao);
        if (app.cubeVbo)
            glDeleteBuffers(1, &app.cubeVbo);
        if (app.cubeEbo)
            glDeleteBuffers(1, &app.cubeEbo);
        if (app.gridVao)
            glDeleteVertexArrays(1, &app.gridVao);
        if (app.gridVbo)
            glDeleteBuffers(1, &app.gridVbo);
        if (app.backgroundVao)
            glDeleteVertexArrays(1, &app.backgroundVao);
        if (app.backgroundVbo)
            glDeleteBuffers(1, &app.backgroundVbo);
        if (app.glowVao)
            glDeleteVertexArrays(1, &app.glowVao);
        if (app.glowVbo)
            glDeleteBuffers(1, &app.glowVbo);
        if (app.glowProgram)
            glDeleteProgram(app.glowProgram);
        if (app.raytraceProgram)
            glDeleteProgram(app.raytraceProgram);
        if (app.raytraceTexture)
            glDeleteTextures(1, &app.raytraceTexture);
        if (app.raytraceFbo)
            glDeleteFramebuffers(1, &app.raytraceFbo);
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplSDL2_Shutdown();
        ImGui::DestroyContext();
        if (app.glContext)
            SDL_GL_DeleteContext(app.glContext);
        if (app.window)
            SDL_DestroyWindow(app.window);
        SDL_Quit();
    }

    AppState* g_app = nullptr;

#if defined(__EMSCRIPTEN__)
    void MainLoop()
    {
        AppState& app = *g_app;
        if (!HandleEvents(app))
        {
            emscripten_cancel_main_loop();
            return;
        }
        const double now = SDL_GetTicks() / 1000.0;
        app.deltaTime = static_cast<float>(now - app.previousTime);
        app.previousTime = now;
        UpdateCamera(app);
        glViewport(0, 0, app.windowWidth, app.windowHeight);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        RenderScene(app);
        UpdateImGui(app);
        SDL_GL_SwapWindow(app.window);
    }
#endif
} // namespace

int main(int, char**)
{
    AppState app{};
    g_app = &app;

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0)
    {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return 1;
    }

#if defined(__EMSCRIPTEN__)
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#else
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
#endif
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    app.window = SDL_CreateWindow("VEngine WebGL",
                                  SDL_WINDOWPOS_CENTERED,
                                  SDL_WINDOWPOS_CENTERED,
                                  app.windowWidth,
                                  app.windowHeight,
                                  SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    if (!app.window)
    {
        SDL_Log("SDL_CreateWindow failed: %s", SDL_GetError());
        Cleanup(app);
        return 1;
    }

    app.glContext = SDL_GL_CreateContext(app.window);
    if (!app.glContext)
    {
        SDL_Log("SDL_GL_CreateContext failed: %s", SDL_GetError());
        Cleanup(app);
        return 1;
    }

    SDL_GL_MakeCurrent(app.window, app.glContext);
    SDL_GL_SetSwapInterval(1);

    glEnable(GL_DEPTH_TEST);
    glClearColor(0.05f, 0.06f, 0.10f, 1.0f);

    CreateBackground(app);
    CreateCube(app);
    CreateGrid(app, 10, 1.0f);
    CreateGlowGeometry(app);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGui_ImplSDL2_InitForOpenGL(app.window, app.glContext);
#if defined(__EMSCRIPTEN__)
    ImGui_ImplOpenGL3_Init("#version 300 es");
#else
    ImGui_ImplOpenGL3_Init("#version 330");
#endif

    app.previousTime = SDL_GetTicks() / 1000.0;

#if defined(__EMSCRIPTEN__)
    emscripten_set_main_loop(MainLoop, 0, 1);
#else
    while (app.running)
    {
        if (!HandleEvents(app))
        {
            break;
        }
        const double now = SDL_GetTicks() / 1000.0;
        app.deltaTime = static_cast<float>(now - app.previousTime);
        app.previousTime = now;
        UpdateCamera(app);
        glViewport(0, 0, app.windowWidth, app.windowHeight);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        RenderScene(app);
        UpdateImGui(app);
        SDL_GL_SwapWindow(app.window);
    }
#endif

    Cleanup(app);
    return 0;
}
