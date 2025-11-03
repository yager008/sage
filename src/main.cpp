#include <windows.h>
#include <windowsx.h>
#include <GL/gl.h>
#include <GL/glu.h>
#include <algorithm>
#include <cmath>
#include <vector>
#include <random>
#include <fstream>
#include <string>
#include <iterator>
#include <unordered_set>
#include <cstring>
#include <cctype>

#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_opengl2.h"
#include "imgui_stdlib.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace
{
    struct Mesh
    {
        std::vector<float> vertices; // xyz triplets
        std::vector<float> normals;  // xyz triplets
    };

    struct PlacedCube
    {
        int gridX;
        int gridZ;
        float r;
        float g;
        float b;
    };

    struct GameState
    {
        float cubeY = 0.0f;
        float cubeVelocity = 0.0f;
        bool grounded = true;
        float rotation = 0.0f;
        float cubeX = 0.0f;
        float cubeZ = 0.0f;
        float moveStartX = 0.0f;
        float moveStartZ = 0.0f;
        float moveTargetX = 0.0f;
        float moveTargetZ = 0.0f;
        float moveTimer = 0.0f;
        float moveDuration = 0.2f;
        bool moving = false;
    };

    bool g_running = true;
    bool g_jumpRequested = false;
    int g_windowWidth = 800;
    int g_windowHeight = 600;
    Mesh g_cubeMesh;
    GameState g_game;
    constexpr float kPi = 3.1415926535f;
    constexpr int kTargetPixelWidth = 320;
    constexpr int kTargetPixelHeight = 180;
    constexpr float kGridCellSize = 1.0f;
    constexpr int kGridHalfSize = 6;
    constexpr int kSnowflakeCount = 300;
    constexpr float kNotesPanelMargin = 20.0f;
    constexpr float kNotesPanelHeightRatio = 0.65f;
    constexpr float kNotesPanelWidthRatio = 0.38f;
    constexpr float kNotesPanelMinWidth = 360.0f;
    constexpr float kNotesPanelMinHeight = 240.0f;
    constexpr float kNotesPanelHiddenOffset = 16.0f;
    constexpr float kNotesPanelSlideSpeed = 12.0f;

    std::string g_notesFilePath;
    std::string g_notesContent;
    bool g_notesDirty = false;
    bool g_showDocs = false;
    std::string g_docsContent =
        "VEngine Engine Docs\n"
        "====================\n\n"
        "Controls:\n"
        "  Arrow keys - move cube on grid\n"
        "  Space      - jump\n\n"
        "Rendering:\n"
        "  Fixed pipeline OpenGL 1.x\n"
        "  Retro post-process via glReadPixels/glDrawPixels\n\n"
        "Code Panel:\n"
        "  Use Save button to persist notes.txt\n\n"
        "Content Panel:\n"
        "  Toggle 'Content' to pick cube presets for spawning\n";

    bool g_cameraMoveForward = false;
    bool g_cameraMoveBackward = false;
    bool g_cameraMoveLeft = false;
    bool g_cameraMoveRight = false;
    float g_cameraFocusX = 0.0f;
    float g_cameraFocusZ = 0.0f;
    float g_cameraDistance = 8.0f;
    constexpr float kCameraMinDistance = 4.0f;
    constexpr float kCameraMaxDistance = 18.0f;
    constexpr float kCameraMoveSpeed = 6.0f;
    constexpr float kCameraZoomStep = 0.6f;
    constexpr float kCameraPitchDegrees = 65.0f;
    std::vector<PlacedCube> g_placedCubes;
    bool g_showContentPanel = false;
    float g_contentPanelPosY = 0.0f;
    constexpr float kContentPanelHeight = 180.0f;
    constexpr float kContentPanelSlideSpeed = 12.0f;

    struct SpawnPreset
    {
        const char* name;
        float r;
        float g;
        float b;
    };

    constexpr SpawnPreset kSpawnPresets[] = {
        {"Blue Cube", 0.3f, 0.45f, 0.85f},
        {"Red Cube", 0.85f, 0.35f, 0.35f},
        {"Grey Cube", 0.65f, 0.65f, 0.65f},
    };
    int g_selectedPresetIndex = 2;

    SpawnPreset GetSelectedPreset()
    {
        const int clamped = std::clamp(g_selectedPresetIndex, 0, static_cast<int>(std::size(kSpawnPresets)) - 1);
        return kSpawnPresets[clamped];
    }

    const std::unordered_set<std::string> kLuaKeywords = {
        "and", "break", "do",   "else", "elseif", "end", "false", "for",   "function",
        "goto", "if",    "in",  "local", "nil",  "not", "or",   "repeat", "return",
        "then", "true",  "until", "while"
    };

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
                const char* tokenStart = ptr++;
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
                RenderLuaToken(drawList, tokenStart, ptr, cursor, stringColor);
                continue;
            }

            if (std::isdigit(static_cast<unsigned char>(*ptr)))
            {
                const char* tokenStart = ptr++;
                while (ptr < lineEnd && (std::isdigit(static_cast<unsigned char>(*ptr)) || *ptr == '.' || *ptr == 'x' || *ptr == 'X'))
                {
                    ++ptr;
                }
                RenderLuaToken(drawList, tokenStart, ptr, cursor, numberColor);
                continue;
            }

            if (IsIdentifierChar(*ptr))
            {
                const char* tokenStart = ptr++;
                while (ptr < lineEnd && IsIdentifierChar(*ptr))
                {
                    ++ptr;
                }
                const std::string identifier(tokenStart, ptr);
                const bool isKeyword = kLuaKeywords.find(identifier) != kLuaKeywords.end();
                RenderLuaToken(drawList, tokenStart, ptr, cursor, isKeyword ? keywordColor : defaultColor);
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
    bool g_notesPanelTargetVisible = true;
    float g_notesPanelPosX = kNotesPanelMargin;

    struct Snowflake
    {
        float x;
        float y;
        float speed;
    };

    std::vector<Snowflake> g_snowflakes;
    int g_snowBufferWidth = 0;
    int g_snowBufferHeight = 0;
    std::mt19937 g_rng{1337u};

    float Lerp(float a, float b, float t)
    {
        return a + (b - a) * t;
    }

    float RandomRange(float minVal, float maxVal)
    {
        std::uniform_real_distribution<float> dist(minVal, maxVal);
        return dist(g_rng);
    }

    std::string BuildNotesFilePath()
    {
        char pathBuffer[MAX_PATH] = {};
        const DWORD length = GetModuleFileNameA(nullptr, pathBuffer, static_cast<DWORD>(std::size(pathBuffer)));
        std::string path(pathBuffer, length);
        const size_t separator = path.find_last_of("\\/");
        if (separator != std::string::npos)
        {
            path.erase(separator + 1);
        }
        else
        {
            path.clear();
        }
        path.append("notes.txt");
        return path;
    }

    void LoadNotesFromFile()
    {
        if (g_notesFilePath.empty())
        {
            return;
        }

        std::ifstream file(g_notesFilePath, std::ios::binary);
        if (!file)
        {
            g_notesContent.clear();
            g_notesDirty = false;
            return;
        }

        std::string contents((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        g_notesContent = std::move(contents);
        g_notesDirty = false;
    }

    void SaveNotesToFile()
    {
        if (g_notesFilePath.empty())
        {
            return;
        }

        std::ofstream file(g_notesFilePath, std::ios::binary);
        if (!file)
        {
            return;
        }
        file.write(g_notesContent.data(), static_cast<std::streamsize>(g_notesContent.size()));
        g_notesDirty = false;
    }

    void EnsureSnowflakes(int renderWidth, int renderHeight)
    {
        if (renderWidth <= 0 || renderHeight <= 0)
        {
            return;
        }

        if (renderWidth == g_snowBufferWidth && renderHeight == g_snowBufferHeight && !g_snowflakes.empty())
        {
            return;
        }

        g_snowflakes.clear();
        g_snowflakes.reserve(kSnowflakeCount);
        g_snowBufferWidth = renderWidth;
        g_snowBufferHeight = renderHeight;

        for (int i = 0; i < kSnowflakeCount; ++i)
        {
            Snowflake flake;
            flake.x = RandomRange(0.0f, static_cast<float>(renderWidth - 1));
            flake.y = RandomRange(0.0f, static_cast<float>(renderHeight - 1));
            flake.speed = RandomRange(15.0f, 45.0f);
            g_snowflakes.push_back(flake);
        }
    }

    void UpdateSnow(float deltaTime, int renderWidth, int renderHeight)
    {
        EnsureSnowflakes(renderWidth, renderHeight);

        if (g_snowflakes.empty())
        {
            return;
        }

        const float height = static_cast<float>(renderHeight);
        for (Snowflake& flake : g_snowflakes)
        {
            flake.y += flake.speed * deltaTime;
            if (flake.y >= height)
            {
                flake.y = 0.0f;
                flake.x = RandomRange(0.0f, static_cast<float>(renderWidth - 1));
                flake.speed = RandomRange(15.0f, 45.0f);
            }
        }
    }

    void OverlaySnow(std::vector<unsigned char>& pixelBuffer, int renderWidth, int renderHeight)
    {
        if (g_snowflakes.empty() || renderWidth <= 0 || renderHeight <= 0)
        {
            return;
        }

        const size_t stride = static_cast<size_t>(renderWidth) * 3u;
        for (const Snowflake& flake : g_snowflakes)
        {
            const int px = std::clamp(static_cast<int>(flake.x), 0, renderWidth - 1);
            const int py = std::clamp(static_cast<int>(flake.y), 0, renderHeight - 1);
            const int row = renderHeight - 1 - py;
            const size_t index = static_cast<size_t>(row) * stride + static_cast<size_t>(px) * 3u;
            if (index + 2 < pixelBuffer.size())
            {
                pixelBuffer[index + 0] = 255;
                pixelBuffer[index + 1] = 255;
                pixelBuffer[index + 2] = 255;
            }
        }
    }

    void RequestMove(int dx, int dz)
    {
        if ((dx == 0 && dz == 0) || g_game.moving)
        {
            return;
        }

        g_game.moving = true;
        g_game.moveTimer = 0.0f;
        g_game.moveStartX = g_game.cubeX;
        g_game.moveStartZ = g_game.cubeZ;
        g_game.moveTargetX = g_game.cubeX + static_cast<float>(dx) * kGridCellSize;
        g_game.moveTargetZ = g_game.cubeZ + static_cast<float>(dz) * kGridCellSize;
    }

    void UpdateGridMovement(float deltaTime)
    {
        if (!g_game.moving)
        {
            return;
        }

        g_game.moveTimer += deltaTime;
        const float duration = std::max(0.0001f, g_game.moveDuration);
        const float progress = std::clamp(g_game.moveTimer / duration, 0.0f, 1.0f);
        g_game.cubeX = Lerp(g_game.moveStartX, g_game.moveTargetX, progress);
        g_game.cubeZ = Lerp(g_game.moveStartZ, g_game.moveTargetZ, progress);

        if (g_game.moveTimer >= duration)
        {
            g_game.cubeX = g_game.moveTargetX;
            g_game.cubeZ = g_game.moveTargetZ;
            g_game.moving = false;
            g_game.moveTimer = duration;
        }
    }

    double GetSeconds()
    {
        static LARGE_INTEGER frequency = [] {
            LARGE_INTEGER value;
            QueryPerformanceFrequency(&value);
            return value;
        }();

        LARGE_INTEGER counter;
        QueryPerformanceCounter(&counter);
        return static_cast<double>(counter.QuadPart) /
               static_cast<double>(frequency.QuadPart);
    }

    void UpdateProjection(int width, int height)
    {
        if (height == 0)
        {
            height = 1;
        }

        glViewport(0, 0, width, height);

        const float aspect = static_cast<float>(width) / static_cast<float>(height);
        const float zNear = 0.1f;
        const float zFar = 100.0f;
        const float fovDegrees = 60.0f;
        const float fovRadians = fovDegrees * kPi / 180.0f;
        const float top = zNear * std::tan(fovRadians * 0.5f);
        const float bottom = -top;
        const float right = top * aspect;
        const float left = -right;

        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        glFrustum(left, right, bottom, top, zNear, zFar);
        glMatrixMode(GL_MODELVIEW);
    }

    Mesh CreateCubeMesh()
    {
        struct VertexData
        {
            float px;
            float py;
            float pz;
            float nx;
            float ny;
            float nz;
        };

        constexpr float s = 0.5f;

        constexpr VertexData vertices[] = {
            {-s, -s, s, 0.0f, 0.0f, 1.0f},
            {s, -s, s, 0.0f, 0.0f, 1.0f},
            {s, s, s, 0.0f, 0.0f, 1.0f},
            {-s, -s, s, 0.0f, 0.0f, 1.0f},
            {s, s, s, 0.0f, 0.0f, 1.0f},
            {-s, s, s, 0.0f, 0.0f, 1.0f},

            {s, -s, s, 1.0f, 0.0f, 0.0f},
            {s, -s, -s, 1.0f, 0.0f, 0.0f},
            {s, s, -s, 1.0f, 0.0f, 0.0f},
            {s, -s, s, 1.0f, 0.0f, 0.0f},
            {s, s, -s, 1.0f, 0.0f, 0.0f},
            {s, s, s, 1.0f, 0.0f, 0.0f},

            {s, -s, -s, 0.0f, 0.0f, -1.0f},
            {-s, -s, -s, 0.0f, 0.0f, -1.0f},
            {-s, s, -s, 0.0f, 0.0f, -1.0f},
            {s, -s, -s, 0.0f, 0.0f, -1.0f},
            {-s, s, -s, 0.0f, 0.0f, -1.0f},
            {s, s, -s, 0.0f, 0.0f, -1.0f},

            {-s, -s, -s, -1.0f, 0.0f, 0.0f},
            {-s, -s, s, -1.0f, 0.0f, 0.0f},
            {-s, s, s, -1.0f, 0.0f, 0.0f},
            {-s, -s, -s, -1.0f, 0.0f, 0.0f},
            {-s, s, s, -1.0f, 0.0f, 0.0f},
            {-s, s, -s, -1.0f, 0.0f, 0.0f},

            {-s, s, s, 0.0f, 1.0f, 0.0f},
            {s, s, s, 0.0f, 1.0f, 0.0f},
            {s, s, -s, 0.0f, 1.0f, 0.0f},
            {-s, s, s, 0.0f, 1.0f, 0.0f},
            {s, s, -s, 0.0f, 1.0f, 0.0f},
            {-s, s, -s, 0.0f, 1.0f, 0.0f},

            {-s, -s, -s, 0.0f, -1.0f, 0.0f},
            {s, -s, -s, 0.0f, -1.0f, 0.0f},
            {s, -s, s, 0.0f, -1.0f, 0.0f},
            {-s, -s, -s, 0.0f, -1.0f, 0.0f},
            {s, -s, s, 0.0f, -1.0f, 0.0f},
            {-s, -s, s, 0.0f, -1.0f, 0.0f},
        };

        Mesh mesh;
        const size_t vertexCount = sizeof(vertices) / sizeof(vertices[0]);
        mesh.vertices.reserve(vertexCount * 3);
        mesh.normals.reserve(vertexCount * 3);

        for (size_t i = 0; i < vertexCount; ++i)
        {
            const VertexData& v = vertices[i];
            mesh.vertices.push_back(v.px);
            mesh.vertices.push_back(v.py);
            mesh.vertices.push_back(v.pz);
            mesh.normals.push_back(v.nx);
            mesh.normals.push_back(v.ny);
            mesh.normals.push_back(v.nz);
        }

        return mesh;
    }

    void RenderGround()
    {
        glDisable(GL_LIGHTING);
        glColor3f(0.2f, 0.2f, 0.2f);
        glBegin(GL_QUADS);
        const float extent = static_cast<float>(kGridHalfSize) * kGridCellSize;
        glVertex3f(-extent, 0.0f, -extent);
        glVertex3f(extent, 0.0f, -extent);
        glVertex3f(extent, 0.0f, extent);
        glVertex3f(-extent, 0.0f, extent);
        glEnd();
        glEnable(GL_LIGHTING);
    }

    void RenderGrid(int halfSize, float cellSize)
    {
        glDisable(GL_LIGHTING);
        glColor3f(0.35f, 0.35f, 0.4f);
        const float extent = static_cast<float>(halfSize) * cellSize;
        glBegin(GL_LINES);
        for (int i = -halfSize; i <= halfSize; ++i)
        {
            const float position = static_cast<float>(i) * cellSize;
            glVertex3f(position, 0.001f, -extent);
            glVertex3f(position, 0.001f, extent);
            glVertex3f(-extent, 0.001f, position);
            glVertex3f(extent, 0.001f, position);
        }
        glEnd();
        glEnable(GL_LIGHTING);
    }

    void RenderMesh(const Mesh& mesh, float r = 0.6f, float g = 0.7f, float b = 1.0f)
    {
        glColor3f(r, g, b);
        glBegin(GL_TRIANGLES);
        const size_t count = mesh.vertices.size();
        for (size_t i = 0; i < count; i += 3)
        {
            glNormal3f(mesh.normals[i], mesh.normals[i + 1], mesh.normals[i + 2]);
            glVertex3f(mesh.vertices[i], mesh.vertices[i + 1], mesh.vertices[i + 2]);
        }
        glEnd();
    }

    void RenderPlacedCubes(const Mesh& mesh)
    {
        for (const PlacedCube& cube : g_placedCubes)
        {
            glPushMatrix();
            glTranslatef(static_cast<float>(cube.gridX), 0.5f, static_cast<float>(cube.gridZ));
            RenderMesh(mesh, cube.r, cube.g, cube.b);
            glPopMatrix();
        }
    }

    void RenderGradientBackground()
    {
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_LIGHTING);

        glMatrixMode(GL_PROJECTION);
        glPushMatrix();
        glLoadIdentity();
        glMatrixMode(GL_MODELVIEW);
        glPushMatrix();
        glLoadIdentity();

        glBegin(GL_QUADS);
        glColor3f(0.08f, 0.05f, 0.12f); // soft pink highlight top
        glVertex2f(-1.0f, 1.0f);
        glVertex2f(1.0f, 1.0f);
        glColor3f(0.03f, 0.05f, 0.12f); // deep blue bottom
        glVertex2f(1.0f, -1.0f);
        glVertex2f(-1.0f, -1.0f);
        glEnd();

        glPopMatrix();
        glMatrixMode(GL_PROJECTION);
        glPopMatrix();
        glMatrixMode(GL_MODELVIEW);

        glEnable(GL_LIGHTING);
        glEnable(GL_DEPTH_TEST);
    }

    void RenderScene(const Mesh& mesh)
    {
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();
        const float pitchRadians = kCameraPitchDegrees * (kPi / 180.0f);
        const float cameraHeight = std::sin(pitchRadians) * g_cameraDistance;
        const float cameraForward = std::cos(pitchRadians) * g_cameraDistance;
        const float eyeX = g_cameraFocusX;
        const float eyeY = cameraHeight;
        const float eyeZ = g_cameraFocusZ + cameraForward;
        gluLookAt(eyeX, eyeY, eyeZ,
                  g_cameraFocusX, 0.0f, g_cameraFocusZ,
                  0.0f, 1.0f, 0.0f);

        const GLfloat lightPos[] = {2.0f, 4.0f, 2.0f, 0.0f};
        glLightfv(GL_LIGHT0, GL_POSITION, lightPos);

        RenderGround();
        RenderGrid(kGridHalfSize, kGridCellSize);

        RenderPlacedCubes(mesh);

        glPushMatrix();
        glTranslatef(g_game.cubeX, g_game.cubeY + 0.5f, g_game.cubeZ);
        glRotatef(g_game.rotation, 0.0f, 1.0f, 0.0f);
        glRotatef(g_game.rotation * 0.5f, 1.0f, 0.0f, 0.0f);
        RenderMesh(mesh);
        glPopMatrix();
    }

    void ApplyRetroPostProcess(int renderWidth, int renderHeight, int scale)
    {
        static std::vector<unsigned char> pixelBuffer;
        const size_t bufferSize = static_cast<size_t>(renderWidth) * static_cast<size_t>(renderHeight) * 3u;
        pixelBuffer.resize(bufferSize);

        glReadBuffer(GL_BACK);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(0, 0, renderWidth, renderHeight, GL_RGB, GL_UNSIGNED_BYTE, pixelBuffer.data());

        for (size_t i = 0; i < bufferSize; ++i)
        {
            pixelBuffer[i] = static_cast<unsigned char>((pixelBuffer[i] >> 2) << 2);
        }

        OverlaySnow(pixelBuffer, renderWidth, renderHeight);

        glViewport(0, 0, g_windowWidth, g_windowHeight);

        glMatrixMode(GL_PROJECTION);
        glPushMatrix();
        glLoadIdentity();
        glOrtho(0, g_windowWidth, 0, g_windowHeight, -1, 1);

        glMatrixMode(GL_MODELVIEW);
        glPushMatrix();
        glLoadIdentity();

        glDisable(GL_DEPTH_TEST);
        glDisable(GL_LIGHTING);

        glClear(GL_COLOR_BUFFER_BIT);

        const int scaledWidth = renderWidth * scale;
        const int scaledHeight = renderHeight * scale;
        const int offsetX = (g_windowWidth - scaledWidth) / 2;
        const int offsetY = (g_windowHeight - scaledHeight) / 2;

        glRasterPos2i(offsetX, offsetY);
        glPixelZoom(static_cast<float>(scale), static_cast<float>(scale));
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glDrawPixels(renderWidth, renderHeight, GL_RGB, GL_UNSIGNED_BYTE, pixelBuffer.data());
        glPixelZoom(1.0f, 1.0f);

        glEnable(GL_LIGHTING);
        glEnable(GL_DEPTH_TEST);

        glPopMatrix();
        glMatrixMode(GL_PROJECTION);
        glPopMatrix();
        glMatrixMode(GL_MODELVIEW);
    }

    void InitializeOpenGLState()
    {
        glEnable(GL_DEPTH_TEST);
        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);
        glEnable(GL_LIGHTING);
        glEnable(GL_LIGHT0);
        glEnable(GL_COLOR_MATERIAL);
        glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE);
        glEnable(GL_NORMALIZE);

        const GLfloat ambient[] = {0.2f, 0.2f, 0.2f, 1.0f};
        const GLfloat diffuse[] = {0.9f, 0.8f, 0.7f, 1.0f};
        const GLfloat specular[] = {0.6f, 0.6f, 0.6f, 1.0f};

        glLightModelfv(GL_LIGHT_MODEL_AMBIENT, ambient);
        glLightfv(GL_LIGHT0, GL_DIFFUSE, diffuse);
        glLightfv(GL_LIGHT0, GL_SPECULAR, specular);
        glMaterialfv(GL_FRONT, GL_SPECULAR, specular);
        glMaterialf(GL_FRONT, GL_SHININESS, 32.0f);
        glClearColor(0.05f, 0.1f, 0.15f, 1.0f);
    }

    LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
    {
        if (ImGui::GetCurrentContext() && ::ImGui_ImplWin32_WndProcHandler(hwnd, uMsg, wParam, lParam))
        {
            return true;
        }

        switch (uMsg)
        {
        case WM_CLOSE:
            SaveNotesToFile();
            g_running = false;
            PostQuitMessage(0);
            return 0;
        case WM_DESTROY:
            return 0;
        case WM_SIZE:
        {
            g_windowWidth = LOWORD(lParam);
            g_windowHeight = HIWORD(lParam);
            if (wglGetCurrentContext())
            {
                UpdateProjection(std::max(1, g_windowWidth), std::max(1, g_windowHeight));
            }
            return 0;
        }
        case WM_KEYDOWN:
            if ((lParam & (1 << 30)) == 0)
            {
                switch (wParam)
                {
                case VK_SPACE:
                    g_jumpRequested = true;
                    return 0;
                case VK_UP:
                    RequestMove(0, -1);
                    return 0;
                case VK_DOWN:
                    RequestMove(0, 1);
                    return 0;
                case VK_LEFT:
                    RequestMove(-1, 0);
                    return 0;
                case VK_RIGHT:
                    RequestMove(1, 0);
                    return 0;
                case 'W':
                    g_cameraMoveForward = true;
                    return 0;
                case 'S':
                    g_cameraMoveBackward = true;
                    return 0;
                case 'A':
                    g_cameraMoveLeft = true;
                    return 0;
                case 'D':
                    g_cameraMoveRight = true;
                    return 0;
                default:
                    break;
                }
            }
            break;
        case WM_KEYUP:
            switch (wParam)
            {
            case 'W':
                g_cameraMoveForward = false;
                return 0;
            case 'S':
                g_cameraMoveBackward = false;
                return 0;
            case 'A':
                g_cameraMoveLeft = false;
                return 0;
            case 'D':
                g_cameraMoveRight = false;
                return 0;
            default:
                break;
            }
            break;
        case WM_MOUSEWHEEL:
        {
            const SHORT delta = GET_WHEEL_DELTA_WPARAM(wParam);
            g_cameraDistance -= static_cast<float>(delta) / 120.0f * kCameraZoomStep;
            g_cameraDistance = std::clamp(g_cameraDistance, kCameraMinDistance, kCameraMaxDistance);
            return 0;
        }
        case WM_LBUTTONDOWN:
        case WM_RBUTTONDOWN:
        {
            if (ImGui::GetIO().WantCaptureMouse)
            {
                break;
            }

            const int mouseX = GET_X_LPARAM(lParam);
            const int mouseY = GET_Y_LPARAM(lParam);

            GLint viewport[4];
            GLdouble modelview[16];
            GLdouble projection[16];

            glGetIntegerv(GL_VIEWPORT, viewport);
            glGetDoublev(GL_MODELVIEW_MATRIX, modelview);
            glGetDoublev(GL_PROJECTION_MATRIX, projection);

            const double winX = static_cast<double>(mouseX);
            const double winY = static_cast<double>(viewport[3] - mouseY);
            GLdouble nearX, nearY, nearZ;
            GLdouble farX, farY, farZ;
            if (gluUnProject(winX, winY, 0.0, modelview, projection, viewport, &nearX, &nearY, &nearZ) == GL_FALSE)
            {
                break;
            }
            if (gluUnProject(winX, winY, 1.0, modelview, projection, viewport, &farX, &farY, &farZ) == GL_FALSE)
            {
                break;
            }

            const double dirX = farX - nearX;
            const double dirY = farY - nearY;
            const double dirZ = farZ - nearZ;
            if (std::fabs(dirY) < 1e-5)
            {
                break;
            }
            const double t = -nearY / dirY;
            if (t < 0.0)
            {
                break;
            }

            const double hitX = nearX + dirX * t;
            const double hitZ = nearZ + dirZ * t;
            const int gridX = static_cast<int>(std::round(hitX / kGridCellSize));
            const int gridZ = static_cast<int>(std::round(hitZ / kGridCellSize));

            if (std::abs(gridX) > kGridHalfSize || std::abs(gridZ) > kGridHalfSize)
            {
                break;
            }

            auto it = std::find_if(g_placedCubes.begin(), g_placedCubes.end(), [gridX, gridZ](const PlacedCube& cube) {
                return cube.gridX == gridX && cube.gridZ == gridZ;
            });

            if (uMsg == WM_LBUTTONDOWN)
            {
                const SpawnPreset preset = GetSelectedPreset();
                if (it == g_placedCubes.end())
                {
                    g_placedCubes.push_back({gridX, gridZ, preset.r, preset.g, preset.b});
                }
                else
                {
                    it->r = preset.r;
                    it->g = preset.g;
                    it->b = preset.b;
                }
            }
            else if (uMsg == WM_RBUTTONDOWN)
            {
                if (it != g_placedCubes.end())
                {
                    g_placedCubes.erase(it);
                }
            }
            return 0;
        }
        default:
            break;
        }

        return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }
}

int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow)
{
    const char kWindowClass[] = "SimpleOpenGLWindow";

    WNDCLASS wc = {};
    wc.style = CS_OWNDC;
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = kWindowClass;

    if (!RegisterClass(&wc))
    {
        MessageBox(nullptr, "Failed to register window class", "Error", MB_OK | MB_ICONERROR);
        return -1;
    }

    RECT desired = {0, 0, g_windowWidth, g_windowHeight};
    AdjustWindowRect(&desired, WS_OVERLAPPEDWINDOW, FALSE);

    ImGui_ImplWin32_EnableDpiAwareness();

    HWND hwnd = CreateWindowEx(
        0,
        kWindowClass,
        "Jumping Cube",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        desired.right - desired.left,
        desired.bottom - desired.top,
        nullptr,
        nullptr,
        hInstance,
        nullptr);

    if (!hwnd)
    {
        MessageBox(nullptr, "Failed to create window", "Error", MB_OK | MB_ICONERROR);
        return -1;
    }

    g_notesFilePath = BuildNotesFilePath();
    LoadNotesFromFile();
    g_notesPanelTargetVisible = true;
    g_notesPanelPosX = kNotesPanelMargin;
    g_contentPanelPosY = static_cast<float>(g_windowHeight) + kContentPanelHeight;

    HDC hdc = GetDC(hwnd);

    PIXELFORMATDESCRIPTOR pfd = {};
    pfd.nSize = sizeof(pfd);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    pfd.cDepthBits = 24;
    pfd.cStencilBits = 8;
    pfd.iLayerType = PFD_MAIN_PLANE;

    const int pixelFormat = ChoosePixelFormat(hdc, &pfd);
    if (pixelFormat == 0 || !SetPixelFormat(hdc, pixelFormat, &pfd))
    {
        MessageBox(nullptr, "Failed to set pixel format", "Error", MB_OK | MB_ICONERROR);
        return -1;
    }

    HGLRC glrc = wglCreateContext(hdc);
    if (!glrc || !wglMakeCurrent(hdc, glrc))
    {
        MessageBox(nullptr, "Failed to create OpenGL context", "Error", MB_OK | MB_ICONERROR);
        return -1;
    }

    g_cubeMesh = CreateCubeMesh();

    InitializeOpenGLState();
    UpdateProjection(std::max(1, g_windowWidth), std::max(1, g_windowHeight));

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.FontDefault = io.Fonts->AddFontDefault();
    ImGui::StyleColorsDark();
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplOpenGL2_Init();

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    MSG msg = {};

    double previousTime = GetSeconds();
    while (g_running)
    {
        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            if (msg.message == WM_QUIT)
            {
                g_running = false;
            }
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

        const double now = GetSeconds();
        float deltaTime = static_cast<float>(now - previousTime);
        previousTime = now;

        if (deltaTime > 0.05f)
        {
            deltaTime = 0.05f;
        }

        ImGui_ImplOpenGL2_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        ImGuiWindowFlags overlayFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings;
        ImGui::SetNextWindowPos(ImVec2(10.0f, 10.0f), ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.6f);
        ImGui::Begin("CodeOverlay", nullptr, overlayFlags);
        const char* toggleLabel = g_notesPanelTargetVisible ? "Hide Code" : "Show Code";
        if (ImGui::Button(toggleLabel))
        {
            g_notesPanelTargetVisible = !g_notesPanelTargetVisible;
        }
        ImGui::SameLine();
        if (ImGui::Button(g_showDocs ? "Docs On" : "Docs"))
        {
            g_showDocs = !g_showDocs;
            if (g_showDocs)
            {
                g_notesPanelTargetVisible = false;
            }
            else
            {
                g_notesPanelTargetVisible = true;
            }
        }
        ImGui::SameLine();
        if (ImGui::Button(g_showContentPanel ? "Content On" : "Content"))
        {
            g_showContentPanel = !g_showContentPanel;
        }
        if (g_notesDirty)
        {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.2f, 1.0f), "unsaved");
        }
        ImGui::End();

        const float panelWidth = std::max(kNotesPanelMinWidth, static_cast<float>(g_windowWidth) * kNotesPanelWidthRatio);
        const float panelHeight = std::max(kNotesPanelMinHeight, static_cast<float>(g_windowHeight) * kNotesPanelHeightRatio);
        const bool anyPanelVisible = g_notesPanelTargetVisible || g_showDocs;
        const float targetPanelX = anyPanelVisible ? kNotesPanelMargin : -panelWidth - kNotesPanelHiddenOffset;
        const float slideAlpha = std::clamp(deltaTime * kNotesPanelSlideSpeed, 0.0f, 1.0f);
        g_notesPanelPosX += (targetPanelX - g_notesPanelPosX) * slideAlpha;

        const bool renderPanelWindow = anyPanelVisible || g_notesPanelPosX > (-panelWidth + 1.0f);
        if (renderPanelWindow)
        {
            const float panelPosY = kNotesPanelMargin + std::max(0.0f, static_cast<float>(g_windowHeight) * 0.05f);
            ImGui::SetNextWindowPos(ImVec2(g_notesPanelPosX, panelPosY), ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(panelWidth, panelHeight), ImGuiCond_Always);
            ImGuiWindowFlags notesFlags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoSavedSettings;
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 6.0f);
            ImGui::Begin("CodePanel", nullptr, notesFlags);
            ImGui::PopStyleVar();

            if (g_showDocs)
            {
                ImGui::TextUnformatted("Documentation");
                ImGui::Separator();
                if (ImGui::Button("Copy##DocsPanel"))
                {
                    ImGui::SetClipboardText(g_docsContent.c_str());
                }
                ImGui::Spacing();

                ImVec2 textSize = ImGui::GetContentRegionAvail();
                if (textSize.y < 120.0f)
                {
                    textSize.y = 120.0f;
                }
                ImGui::InputTextMultiline("##DocsContent",
                                          &g_docsContent,
                                          textSize,
                                          ImGuiInputTextFlags_ReadOnly | ImGuiInputTextFlags_AllowTabInput | ImGuiInputTextFlags_NoUndoRedo);
            }
            else
            {
                ImGui::TextUnformatted("Code");
                ImGui::Separator();
                if (ImGui::Button("Save##CodePanel"))
                {
                    SaveNotesToFile();
                }
                ImGui::SameLine();
                ImGui::TextUnformatted(g_notesDirty ? "Modified" : "Saved");
                ImGui::Spacing();

                ImVec2 textSize = ImGui::GetContentRegionAvail();
                ImVec2 editorPos = ImGui::GetCursorScreenPos();
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0, 0, 0, 0));
                const bool codeEdited = ImGui::InputTextMultiline("##CodeContent",
                                                                  &g_notesContent,
                                                                  textSize,
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
                                                                              const char* text = data->Buf;
                                                                              const int pos = data->CursorPos;
                                                                              int lineStart = pos - 1;
                                                                              while (lineStart >= 0 && text[lineStart] != '\n')
                                                                              {
                                                                                  --lineStart;
                                                                              }
                                                                              ++lineStart;
                                                                              int indentCount = 0;
                                                                              while (text[lineStart] == ' ')
                                                                              {
                                                                                  ++indentCount;
                                                                                  ++lineStart;
                                                                              }
                                                                              bool hasFunction = false;
                                                                              for (int i = lineStart; text[i] && text[i] != '\n'; ++i)
                                                                              {
                                                                                  if (!std::isspace(static_cast<unsigned char>(text[i])))
                                                                                  {
                                                                                      const char* keyword = "function";
                                                                                      const int keywordLen = 8;
                                                                                      if (std::strncmp(&text[i], keyword, keywordLen) == 0)
                                                                                      {
                                                                                          hasFunction = true;
                                                                                      }
                                                                                      break;
                                                                                  }
                                                                              }

                                                                              if (pos > 0 && text[pos - 1] == '\n')
                                                                              {
                                                                                  std::string indent(indentCount, ' ');
                                                                                  if (hasFunction)
                                                                                  {
                                                                                      indent.append(kTab);
                                                                                  }
                                                                                  data->InsertChars(data->CursorPos, indent.c_str());
                                                                              }
                                                                              else if (pos > 0 && pos <= data->BufTextLen && text[pos - 1] == 'd')
                                                                              {
                                                                                  const char* endKeyword = "end";
                                                                                  if (pos >= 3 && std::strncmp(&text[pos - 3], endKeyword, 3) == 0)
                                                                                  {
                                                                                      int removeCount = std::min(indentCount, 4);
                                                                                      int removeStart = pos - 3;
                                                                                      while (removeCount > 0 && removeStart > 0 && text[removeStart - 1] == ' ')
                                                                                      {
                                                                                          --removeStart;
                                                                                          --removeCount;
                                                                                      }
                                                                                      data->DeleteChars(removeStart, pos - 3 - removeStart);
                                                                                      data->CursorPos = removeStart + 3;
                                                                                  }
                                                                              }
                                                                          }
                                                                      }
                                                                      return 0;
                                                                  });
                ImGui::PopStyleColor();
                if (codeEdited)
                {
                    g_notesDirty = true;
                }
                RenderLuaHighlightedText(g_notesContent, editorPos, textSize);
            }

            ImGui::End();
        }

        const float contentTargetY = g_showContentPanel ?
                                          std::max(0.0f, static_cast<float>(g_windowHeight) - kContentPanelHeight - kNotesPanelMargin) :
                                          static_cast<float>(g_windowHeight) + kContentPanelHeight + kNotesPanelMargin;
        const float contentSlideAlpha = std::clamp(deltaTime * kContentPanelSlideSpeed, 0.0f, 1.0f);
        g_contentPanelPosY += (contentTargetY - g_contentPanelPosY) * contentSlideAlpha;

        const float contentPanelWidth = std::max(360.0f, static_cast<float>(g_windowWidth) * 0.5f);
        const bool renderContentPanel = g_showContentPanel || g_contentPanelPosY < static_cast<float>(g_windowHeight) + kContentPanelHeight - 1.0f;
        if (renderContentPanel)
        {
            ImGui::SetNextWindowPos(ImVec2((static_cast<float>(g_windowWidth) - contentPanelWidth) * 0.5f, g_contentPanelPosY), ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(contentPanelWidth, kContentPanelHeight), ImGuiCond_Always);
            ImGuiWindowFlags contentFlags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings;
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 6.0f);
            ImGui::Begin("ContentPanel", nullptr, contentFlags);
            ImGui::PopStyleVar();

            ImGui::TextUnformatted("Content Browser");
            ImGui::Separator();
            ImGui::TextWrapped("Select a preset and click on the grid to spawn it. Use right click to delete.");
            ImGui::Spacing();

            for (int i = 0; i < static_cast<int>(std::size(kSpawnPresets)); ++i)
            {
                const SpawnPreset& preset = kSpawnPresets[i];
                ImGui::PushID(i);
                ImVec4 color(preset.r, preset.g, preset.b, 1.0f);
                ImGui::ColorButton("##Color", color, ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoBorder, ImVec2(24.0f, 24.0f));
                ImGui::SameLine();
                if (ImGui::Selectable(preset.name, g_selectedPresetIndex == i))
                {
                    g_selectedPresetIndex = i;
                }
                ImGui::PopID();
            }

            ImGui::End();
        }

        if (!ImGui::GetIO().WantCaptureKeyboard)
        {
            float moveX = 0.0f;
            float moveZ = 0.0f;
            if (g_cameraMoveForward)
            {
                moveZ -= 1.0f;
            }
            if (g_cameraMoveBackward)
            {
                moveZ += 1.0f;
            }
            if (g_cameraMoveLeft)
            {
                moveX -= 1.0f;
            }
            if (g_cameraMoveRight)
            {
                moveX += 1.0f;
            }

            const float lengthSq = moveX * moveX + moveZ * moveZ;
            if (lengthSq > 0.0001f)
            {
                const float invLen = 1.0f / std::sqrt(lengthSq);
                moveX *= invLen;
                moveZ *= invLen;
                g_cameraFocusX += moveX * kCameraMoveSpeed * deltaTime;
                g_cameraFocusZ += moveZ * kCameraMoveSpeed * deltaTime;
            }
        }

        if (g_jumpRequested && g_game.grounded)
        {
            g_game.cubeVelocity = 4.5f;
            g_game.grounded = false;
        }
        g_jumpRequested = false;

        UpdateGridMovement(deltaTime);

        const float gravity = -9.8f;
        g_game.cubeVelocity += gravity * deltaTime;
        g_game.cubeY += g_game.cubeVelocity * deltaTime;

        if (g_game.cubeY <= 0.0f)
        {
            g_game.cubeY = 0.0f;
            g_game.cubeVelocity = 0.0f;
            g_game.grounded = true;
        }

        g_game.rotation += 45.0f * deltaTime;
        if (g_game.rotation > 360.0f)
        {
            g_game.rotation -= 360.0f;
        }
        const int scaleX = std::max(1, g_windowWidth / kTargetPixelWidth);
        const int scaleY = std::max(1, g_windowHeight / kTargetPixelHeight);
        const int scale = std::max(1, std::min(scaleX, scaleY));
        const int renderWidth = std::max(1, g_windowWidth / scale);
        const int renderHeight = std::max(1, g_windowHeight / scale);

        glViewport(0, 0, g_windowWidth, g_windowHeight);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glViewport(0, 0, renderWidth, renderHeight);
        UpdateProjection(renderWidth, renderHeight);

        RenderGradientBackground();
        RenderScene(g_cubeMesh);

        UpdateSnow(deltaTime, renderWidth, renderHeight);
        ApplyRetroPostProcess(renderWidth, renderHeight, scale);
        ImGui::Render();
        ImGui_ImplOpenGL2_RenderDrawData(ImGui::GetDrawData());
        SwapBuffers(hdc);
    }

    if (g_notesDirty)
    {
        SaveNotesToFile();
    }

    ImGui_ImplOpenGL2_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    wglMakeCurrent(nullptr, nullptr);
    if (glrc)
    {
        wglDeleteContext(glrc);
    }

    if (hdc)
    {
        ReleaseDC(hwnd, hdc);
    }

    DestroyWindow(hwnd);

    return static_cast<int>(msg.wParam);
}
