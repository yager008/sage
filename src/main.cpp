#include <windows.h>
#include <windowsx.h>
#include <gdiplus.h>
#include <GL/gl.h>
#include <GL/glu.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <vector>
#include <random>
#include <fstream>
#include <string>
#include <iterator>
#include <unordered_set>
#include <cstring>
#include <cctype>
#include <filesystem>
#include <sstream>
#include <iomanip>
#include <filesystem>
#include <sstream>

#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif

#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_opengl2.h"
#include "imgui_stdlib.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace
{
    constexpr int kInvalidTextureHandle = -1; // Used when a cube has no PNG texture assigned.

    ULONG_PTR g_gdiplusToken = 0; // Shared GDI+ session for PNG decoding.

    bool EnsureGdiplusInitialized()
    {
        if (g_gdiplusToken != 0)
        {
            return true;
        }

        Gdiplus::GdiplusStartupInput input;
        return Gdiplus::GdiplusStartup(&g_gdiplusToken, &input, nullptr) == Gdiplus::Ok;
    }

    void ShutdownGdiplus()
    {
        if (g_gdiplusToken != 0)
        {
            Gdiplus::GdiplusShutdown(g_gdiplusToken);
            g_gdiplusToken = 0;
        }
    }

    std::wstring Utf8ToWide(const std::string& input)
    {
        if (input.empty())
        {
            return std::wstring();
        }
        const int required = MultiByteToWideChar(CP_UTF8, 0, input.c_str(), -1, nullptr, 0);
        if (required <= 0)
        {
            return std::wstring();
        }
        std::wstring result(static_cast<size_t>(required) - 1, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, input.c_str(), -1, result.data(), required);
        return result;
    }

    bool LoadImagePixelsGdiplus(const std::string& path, std::vector<unsigned char>& outPixels, UINT& width, UINT& height, std::string& errorMessage)
    {
        if (!EnsureGdiplusInitialized())
        {
            errorMessage = "Failed to initialize GDI+";
            return false;
        }

        std::wstring widePath = Utf8ToWide(path);
        if (widePath.empty())
        {
            errorMessage = "Invalid path";
            return false;
        }

        Gdiplus::Bitmap bitmap(widePath.c_str());
        if (bitmap.GetLastStatus() != Gdiplus::Ok)
        {
            errorMessage = "Unable to open image";
            return false;
        }

        width = bitmap.GetWidth();
        height = bitmap.GetHeight();
        if (width == 0 || height == 0)
        {
            errorMessage = "Image has no size";
            return false;
        }

        Gdiplus::Rect rect(0, 0, width, height);
        Gdiplus::BitmapData data;
        if (bitmap.LockBits(&rect, Gdiplus::ImageLockModeRead, PixelFormat32bppARGB, &data) != Gdiplus::Ok)
        {
            errorMessage = "LockBits failed";
            return false;
        }

        outPixels.resize(static_cast<size_t>(width) * static_cast<size_t>(height) * 4u);
        for (UINT y = 0; y < height; ++y)
        {
            const unsigned char* srcRow = static_cast<const unsigned char*>(data.Scan0) + y * data.Stride;
            for (UINT x = 0; x < width; ++x)
            {
                const size_t dstIndex = (static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)) * 4u;
                const unsigned char b = srcRow[x * 4 + 0];
                const unsigned char g = srcRow[x * 4 + 1];
                const unsigned char r = srcRow[x * 4 + 2];
                const unsigned char a = srcRow[x * 4 + 3];
                outPixels[dstIndex + 0] = r;
                outPixels[dstIndex + 1] = g;
                outPixels[dstIndex + 2] = b;
                outPixels[dstIndex + 3] = a;
            }
        }

        bitmap.UnlockBits(&data);
        return true;
    }

    struct LoadedTexture
    {
        GLuint id = 0;
        int width = 0;
        int height = 0;
        std::string path;
    };

    std::vector<LoadedTexture> g_loadedTextures;

    const LoadedTexture* GetTextureInfo(int handle)
    {
        if (handle < 0 || static_cast<size_t>(handle) >= g_loadedTextures.size())
        {
            return nullptr;
        }
        return &g_loadedTextures[static_cast<size_t>(handle)];
    }

    int FindTextureHandleByPath(const std::string& path)
    {
        for (size_t i = 0; i < g_loadedTextures.size(); ++i)
        {
            if (g_loadedTextures[i].path == path)
            {
                return static_cast<int>(i);
            }
        }
        return kInvalidTextureHandle;
    }

    int LoadTextureFromFile(const std::string& path, std::string& messageOut)
    {
        if (path.empty())
        {
            messageOut = "Path is empty";
            return kInvalidTextureHandle;
        }

        const int existing = FindTextureHandleByPath(path);
        if (existing >= 0)
        {
            messageOut = "Texture cached";
            return existing;
        }

        std::vector<unsigned char> pixels;
        UINT width = 0;
        UINT height = 0;
        if (!LoadImagePixelsGdiplus(path, pixels, width, height, messageOut))
        {
            return kInvalidTextureHandle;
        }

        GLuint texId = 0;
        glGenTextures(1, &texId);
        if (texId == 0)
        {
            messageOut = "glGenTextures failed";
            return kInvalidTextureHandle;
        }

        glBindTexture(GL_TEXTURE_2D, texId);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D,
                     0,
                     GL_RGBA8,
                     static_cast<GLsizei>(width),
                     static_cast<GLsizei>(height),
                     0,
                     GL_RGBA,
                     GL_UNSIGNED_BYTE,
                     pixels.data());
        glBindTexture(GL_TEXTURE_2D, 0);

        LoadedTexture info;
        info.id = texId;
        info.width = static_cast<int>(width);
        info.height = static_cast<int>(height);
        info.path = path;
        g_loadedTextures.push_back(info);

        messageOut = "Loaded";
        return static_cast<int>(g_loadedTextures.size() - 1);
    }

    void CleanupLoadedTextures()
    {
        for (LoadedTexture& tex : g_loadedTextures)
        {
            if (tex.id != 0)
            {
                glDeleteTextures(1, &tex.id);
                tex.id = 0;
            }
        }
        g_loadedTextures.clear();
    }

    // CPU-side copy of the cube mesh. Each vertex stores position, normal, UV for retro GL.
    struct Mesh
    {
        std::vector<float> vertices; // xyz triplets
        std::vector<float> normals;  // xyz triplets
        std::vector<float> texcoords; // uv pairs
    };

    // Runtime state for a block dropped into the world. Each cube remembers its texture handle.
    struct PlacedCube
    {
        int gridX;
        int gridY;
        int gridZ;
        float r;
        float g;
        float b;
        bool glowing = false;
        bool transparent = false;
        int textureHandle = kInvalidTextureHandle;
        int presetIndex = -1;
        std::string texturePath;
    };

    struct GameState
    {
        float cubeY = 0.0f;
        float cubeVelocity = 0.0f;
        bool grounded = true;
        float rotation = 0.0f;
        float cubeX = 0.0f;
        float cubeZ = 0.0f;
        float velX = 0.0f;
        float velZ = 0.0f;
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

    struct ColorRGB
    {
        float r;
        float g;
        float b;
    };

    struct Vec3
    {
        float x;
        float y;
        float z;

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

    std::string g_exeDirectory;
    std::string g_notesFilePath;
    std::string g_sceneFilePath;
    bool g_sceneSuppressSave = false;
    std::string g_notesContent;
    bool g_notesDirty = false;
    bool g_showDocs = false;
    std::string g_docsContent =
        "GYGE - Gerasin Yaroslav Game Engine\n"
        "Version 0.001\n"
        "Last update: 10.11.2025\n";

    float g_cameraYawDegrees = 0.0f;
    float g_cameraYawTarget = 0.0f;
    float g_cameraPitchDegrees = 65.0f;
    float g_cameraPitchTarget = 65.0f;
    bool g_moveForward = false;
    bool g_moveBackward = false;
    bool g_moveLeft = false;
    bool g_moveRight = false;
    float g_cameraFocusX = 0.0f;
    float g_cameraFocusZ = 0.0f;
    float g_cameraDistance = 8.0f;
    constexpr float kCameraMinDistance = 4.0f;
    constexpr float kCameraMaxDistance = 18.0f;
    constexpr float kCameraMoveSpeed = 6.0f;
    constexpr float kCameraZoomStep = 0.6f;
    constexpr float kCameraPitchMinDegrees = 25.0f;
    constexpr float kCameraPitchMaxDegrees = 85.0f;
    constexpr float kCameraPitchStepDegrees = 6.0f;
    constexpr float kCameraPitchSpeed = 180.0f;
    constexpr float kCameraRotationSpeed = 240.0f;
    std::vector<PlacedCube> g_placedCubes;
    bool g_showContentPanel = false;
    bool g_showEnvironmentPanel = false;
    float g_contentPanelPosY = 0.0f;
    constexpr float kContentPanelHeight = 180.0f;
    constexpr float kContentPanelSlideSpeed = 12.0f;
    ColorRGB g_gradientTop{0.18f, 0.13f, 0.25f};
    ColorRGB g_gradientBottom{0.03f, 0.05f, 0.12f};
    constexpr float kPlayerRadius = 0.35f;
    constexpr float kPlayerHeight = 1.0f;
    constexpr float kStepHeight = 1.0f;
    // State machine for the long-press “pick up & drag” feature.
    bool g_draggingCube = false;
    PlacedCube g_draggedCube;
    bool g_dragPreviewValid = false;
    bool g_dragPreviewHasPosition = false;
    int g_dragPreviewX = 0;
    int g_dragPreviewY = 0;
    int g_dragPreviewZ = 0;
    bool g_pendingCubeDrag = false;
    int g_pendingDragIndex = -1;
    int g_pendingPlacementPresetIndex = -1;
    POINT g_pendingDragStartPos{0, 0};
    double g_pendingDragStartTime = 0.0;
    constexpr int kDragStartPixelThreshold = 4;
    constexpr double kDragStartHoldSeconds = 0.12;

    bool g_sceneDirty = false;

    Vec3 CameraForward2D()
    {
        const float yawRadians = g_cameraYawDegrees * (kPi / 180.0f);
        return Vec3{-std::sin(yawRadians), 0.0f, -std::cos(yawRadians)};
    }

    Vec3 CameraRight2D()
    {
        Vec3 forward = CameraForward2D();
        return Vec3{-forward.z, 0.0f, forward.x};
    }

    void NormalizeAngle(float& angle)
    {
        while (angle >= 360.0f)
        {
            angle -= 360.0f;
        }
        while (angle < 0.0f)
        {
            angle += 360.0f;
        }
    }

    float ShortestAngleDelta(float from, float to)
    {
        float delta = to - from;
        while (delta > 180.0f)
        {
            delta -= 360.0f;
        }
        while (delta < -180.0f)
        {
            delta += 360.0f;
        }
        return delta;
    }

    struct SpawnPreset
    {
        const char* name;
        float r;
        float g;
        float b;
        bool glowing;
        bool transparent;
    };

    float ComputeLightAtPoint(const Vec3& point, const PlacedCube* receiver);

    constexpr SpawnPreset kSpawnPresets[] = {
        {"Blue Cube", 0.3f, 0.45f, 0.85f, false, false},
        {"Red Cube", 0.85f, 0.35f, 0.35f, false, false},
        {"Grey Cube", 0.65f, 0.65f, 0.65f, false, false},
        {"Green Cube", 0.35f, 0.75f, 0.45f, false, false},
        {"Glow Cube", 1.0f, 0.92f, 0.5f, true, false},
        {"Glass Cube", 0.75f, 0.9f, 1.0f, false, true},
    };
    int g_selectedPresetIndex = 2;

    constexpr size_t kSpawnPresetCount = std::size(kSpawnPresets);

    // Current texture handle/status per preset (managed by the Content Browser).
    std::array<int, kSpawnPresetCount> g_presetTextureHandles = [] {
        std::array<int, kSpawnPresetCount> handles{};
        handles.fill(kInvalidTextureHandle);
        return handles;
    }();

    std::array<std::string, kSpawnPresetCount> g_presetTexturePaths;
    std::array<std::string, kSpawnPresetCount> g_presetTextureStatus;

    int FindCubeIndex(int x, int y, int z)
    {
        for (size_t i = 0; i < g_placedCubes.size(); ++i)
        {
            const PlacedCube& cube = g_placedCubes[i];
            if (cube.gridX == x && cube.gridY == y && cube.gridZ == z)
            {
                return static_cast<int>(i);
            }
        }
        return -1;
    }

    int FindHighestCubeIndex(int x, int z)
    {
        int bestIndex = -1;
        int bestHeight = std::numeric_limits<int>::min();
        for (size_t i = 0; i < g_placedCubes.size(); ++i)
        {
            const PlacedCube& cube = g_placedCubes[i];
            if (cube.gridX == x && cube.gridZ == z && cube.gridY > bestHeight)
            {
                bestHeight = cube.gridY;
                bestIndex = static_cast<int>(i);
            }
        }
        return bestIndex;
    }

    void MarkSceneDirty()
    {
        if (!g_sceneSuppressSave)
        {
            g_sceneDirty = true;
        }
    }

    void PlaceCube(int x, int y, int z, const SpawnPreset& preset, int presetIndex, int textureHandle, const std::string& texturePath)
    {
        if (std::abs(x) > 100 || std::abs(z) > 100)
        {
            return;
        }
        if (FindCubeIndex(x, y, z) >= 0)
        {
            return;
        }
        PlacedCube cube{x, y, z, preset.r, preset.g, preset.b, preset.glowing, preset.transparent, textureHandle, presetIndex, texturePath};
        g_placedCubes.push_back(std::move(cube));
        MarkSceneDirty();
    }

    void RemoveCube(int x, int y, int z)
    {
        int index = FindCubeIndex(x, y, z);
        if (index >= 0)
        {
            g_placedCubes.erase(g_placedCubes.begin() + index);
            MarkSceneDirty();
        }
    }

    struct RayHit
    {
        bool hit = false;
        bool hitCube = false;
        bool hitGround = false;
        float t = std::numeric_limits<float>::max();
        int cubeX = 0;
        int cubeY = 0;
        int cubeZ = 0;
        int groundX = 0;
        int groundZ = 0;
        Vec3 normal{0.0f, 1.0f, 0.0f};
    };

    bool RayIntersectsAABB(const Vec3& origin, const Vec3& dir, const Vec3& minB, const Vec3& maxB, float& tOut, Vec3& normalOut)
    {
        float tMin = 0.0f;
        float tMax = std::numeric_limits<float>::max();
        Vec3 normal{0.0f, 0.0f, 0.0f};
        int hitAxis = -1;
        float hitSign = 0.0f;

        auto checkAxis = [&](float originComponent, float dirComponent, float minVal, float maxVal, int axis) -> bool {
            if (std::fabs(dirComponent) < 1e-6f)
            {
                if (originComponent < minVal || originComponent > maxVal)
                {
                    return false;
                }
                return true;
            }

            float invD = 1.0f / dirComponent;
            float t1 = (minVal - originComponent) * invD;
            float t2 = (maxVal - originComponent) * invD;
            float sign = -1.0f;
            if (t1 > t2)
            {
                std::swap(t1, t2);
                sign = 1.0f;
            }
            if (t1 > tMin)
            {
                tMin = t1;
                hitAxis = axis;
                hitSign = sign;
            }
            tMax = std::min(tMax, t2);
            if (tMax < tMin)
            {
                return false;
            }
            return true;
        };

        if (!checkAxis(origin.x, dir.x, minB.x, maxB.x, 0) ||
            !checkAxis(origin.y, dir.y, minB.y, maxB.y, 1) ||
            !checkAxis(origin.z, dir.z, minB.z, maxB.z, 2))
        {
            return false;
        }

        if (hitAxis == 0)
        {
            normal = Vec3{hitSign, 0.0f, 0.0f};
        }
        else if (hitAxis == 1)
        {
            normal = Vec3{0.0f, hitSign, 0.0f};
        }
        else if (hitAxis == 2)
        {
            normal = Vec3{0.0f, 0.0f, hitSign};
        }

        tOut = tMin;
        normalOut = normal;
        return true;
    }

    RayHit CastWorldRay(const Vec3& origin, const Vec3& dir)
    {
        RayHit result;
        if (std::fabs(dir.y) > 1e-6f)
        {
            float t = -origin.y / dir.y;
            if (t > 0.0f)
            {
                Vec3 hitPoint = origin + dir * t;
                int gx = static_cast<int>(std::round(hitPoint.x));
                int gz = static_cast<int>(std::round(hitPoint.z));
                if (std::fabs(hitPoint.x) <= 200.0f && std::fabs(hitPoint.z) <= 200.0f)
                {
                    result.hit = true;
                    result.hitGround = true;
                    result.hitCube = false;
                    result.t = t;
                    result.groundX = gx;
                    result.groundZ = gz;
                    result.normal = Vec3{0.0f, 1.0f, 0.0f};
                }
            }
        }

        for (const PlacedCube& cube : g_placedCubes)
        {
            Vec3 minB{static_cast<float>(cube.gridX) - 0.5f, static_cast<float>(cube.gridY), static_cast<float>(cube.gridZ) - 0.5f};
            Vec3 maxB{static_cast<float>(cube.gridX) + 0.5f, static_cast<float>(cube.gridY) + 1.0f, static_cast<float>(cube.gridZ) + 0.5f};
            float t = 0.0f;
            Vec3 normal;
            if (RayIntersectsAABB(origin, dir, minB, maxB, t, normal) && t > 0.0f && t < result.t)
            {
                result.hit = true;
                result.hitCube = true;
                result.hitGround = false;
                result.t = t;
                result.cubeX = cube.gridX;
                result.cubeY = cube.gridY;
                result.cubeZ = cube.gridZ;
                result.normal = normal;
            }
        }

        return result;
    }

    RayHit g_pendingDragHit;

    bool ComputePlacementTarget(const RayHit& hit, int& outX, int& outY, int& outZ)
    {
        if (!hit.hit)
        {
            return false;
        }
        if (hit.hitCube)
        {
            const int offsetX = static_cast<int>(std::round(hit.normal.x));
            const int offsetY = static_cast<int>(std::round(hit.normal.y));
            const int offsetZ = static_cast<int>(std::round(hit.normal.z));
            outX = hit.cubeX + offsetX;
            outY = hit.cubeY + offsetY;
            outZ = hit.cubeZ + offsetZ;
            return true;
        }
        if (hit.hitGround)
        {
            outX = hit.groundX;
            outZ = hit.groundZ;
            int topIndex = FindHighestCubeIndex(outX, outZ);
            if (topIndex >= 0)
            {
                outY = g_placedCubes[topIndex].gridY + 1;
            }
            else
            {
                outY = -1;
            }
            return true;
        }
        return false;
    }

    bool ComputeRayFromScreen(int mouseX, int mouseY, Vec3& origin, Vec3& direction)
    {
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
            return false;
        }
        if (gluUnProject(winX, winY, 1.0, modelview, projection, viewport, &farX, &farY, &farZ) == GL_FALSE)
        {
            return false;
        }

        origin = Vec3{static_cast<float>(nearX), static_cast<float>(nearY), static_cast<float>(nearZ)};
        direction = Vec3{static_cast<float>(farX - nearX), static_cast<float>(farY - nearY), static_cast<float>(farZ - nearZ)};
        return true;
    }

    void UpdateDraggingCubePreview(int mouseX, int mouseY)
    {
        if (!g_draggingCube)
        {
            return;
        }
        Vec3 origin;
        Vec3 direction;
        if (!ComputeRayFromScreen(mouseX, mouseY, origin, direction))
        {
            g_dragPreviewHasPosition = false;
            g_dragPreviewValid = false;
            return;
        }
        RayHit hit = CastWorldRay(origin, direction);
        int targetX = 0;
        int targetY = 0;
        int targetZ = 0;
        if (!ComputePlacementTarget(hit, targetX, targetY, targetZ))
        {
            g_dragPreviewHasPosition = false;
            g_dragPreviewValid = false;
            return;
        }

        g_dragPreviewX = targetX;
        g_dragPreviewY = targetY;
        g_dragPreviewZ = targetZ;
        g_dragPreviewHasPosition = true;

        if (std::abs(targetX) > 100 || std::abs(targetZ) > 100)
        {
            g_dragPreviewValid = false;
            return;
        }
        if (FindCubeIndex(targetX, targetY, targetZ) >= 0)
        {
            g_dragPreviewValid = false;
            return;
        }
        g_dragPreviewValid = true;
    }

    void CancelPendingCubeDrag()
    {
        g_pendingCubeDrag = false;
        g_pendingDragIndex = -1;
        g_pendingPlacementPresetIndex = -1;
    }

    void BeginCubeDrag(int cubeIndex, int mouseX, int mouseY)
    {
        if (cubeIndex < 0 || cubeIndex >= static_cast<int>(g_placedCubes.size()))
        {
            CancelPendingCubeDrag();
            return;
        }
        g_draggingCube = true;
        g_draggedCube = g_placedCubes[cubeIndex];
        g_placedCubes.erase(g_placedCubes.begin() + cubeIndex);
        g_dragPreviewHasPosition = false;
        g_dragPreviewValid = false;
        UpdateDraggingCubePreview(mouseX, mouseY);
        g_pendingPlacementPresetIndex = -1;
    }

    void FinishCubeDrag(bool commit)
    {
        if (!g_draggingCube)
        {
            return;
        }
        bool appliedNewPosition = false;
        if (commit && g_dragPreviewValid && g_dragPreviewHasPosition)
        {
            g_draggedCube.gridX = g_dragPreviewX;
            g_draggedCube.gridY = g_dragPreviewY;
            g_draggedCube.gridZ = g_dragPreviewZ;
            appliedNewPosition = true;
        }
        g_placedCubes.push_back(g_draggedCube);
        if (appliedNewPosition)
        {
            MarkSceneDirty();
        }
        g_draggingCube = false;
        g_dragPreviewValid = false;
        g_dragPreviewHasPosition = false;
        g_draggedCube = {};
        ReleaseCapture();
        g_pendingPlacementPresetIndex = -1;
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

    float RandomRange(float minVal, float maxVal)
    {
        std::uniform_real_distribution<float> dist(minVal, maxVal);
        return dist(g_rng);
    }

    std::string GetExecutableDirectory()
    {
        if (!g_exeDirectory.empty())
        {
            return g_exeDirectory;
        }
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
        g_exeDirectory = path;
        return g_exeDirectory;
    }

    std::string BuildNotesFilePath()
    {
        std::string path = GetExecutableDirectory();
        path.append("notes.txt");
        return path;
    }

    std::string BuildSceneFilePath()
    {
        std::string path = GetExecutableDirectory();
        path.append("scene.txt");
        return path;
    }

    std::string MakeAbsoluteTexturePath(const std::string& storedPath)
    {
        if (storedPath.empty())
        {
            return {};
        }
        namespace fs = std::filesystem;
        fs::path base(GetExecutableDirectory());
        fs::path rel(storedPath);
        fs::path combined = rel.is_absolute() ? rel : (base / rel);
        return combined.string();
    }

    bool NormalizeTextureInputPath(const std::string& inputPath, std::string& relativeOut, std::string& statusOut)
    {
        if (inputPath.empty())
        {
            statusOut = "Path is empty";
            return false;
        }

        namespace fs = std::filesystem;
        std::error_code ec;
        fs::path base(GetExecutableDirectory());
        fs::path canonicalBase = fs::weakly_canonical(base, ec);
        if (ec)
        {
            statusOut = "Cannot resolve exe directory";
            return false;
        }

        fs::path candidate(inputPath);
        if (!candidate.is_absolute())
        {
            candidate = canonicalBase / candidate;
        }
        fs::path canonicalCandidate = fs::weakly_canonical(candidate, ec);
        if (ec)
        {
            statusOut = "Invalid path";
            return false;
        }

        fs::path relative = fs::relative(canonicalCandidate, canonicalBase, ec);
        if (ec)
        {
            statusOut = "Texture must be inside exe directory";
            return false;
        }
        std::string rel = relative.generic_string();
        if (rel.empty() || rel.rfind("..", 0) == 0)
        {
            statusOut = "Texture must stay inside exe directory";
            return false;
        }

        relativeOut = rel;
        statusOut.clear();
        return true;
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

    bool SaveSceneToFile()
    {
        if (g_sceneFilePath.empty() || g_sceneSuppressSave)
        {
            return false;
        }

        std::ofstream file(g_sceneFilePath, std::ios::binary);
        if (!file)
        {
            return false;
        }

        file << "VENGINE_SCENE 1\n";
        file << g_placedCubes.size() << '\n';
        for (const PlacedCube& cube : g_placedCubes)
        {
            file << cube.gridX << ' '
                 << cube.gridY << ' '
                 << cube.gridZ << ' '
                 << cube.r << ' '
                 << cube.g << ' '
                 << cube.b << ' '
                 << (cube.glowing ? 1 : 0) << ' '
                 << (cube.transparent ? 1 : 0) << ' '
                 << cube.presetIndex << ' '
                 << std::quoted(cube.texturePath) << '\n';
        }
        g_sceneDirty = false;
        return true;
    }

    void LoadSceneFromFile()
    {
        if (g_sceneFilePath.empty())
        {
            return;
        }

        g_sceneSuppressSave = true;
        g_placedCubes.clear();

        std::ifstream file(g_sceneFilePath, std::ios::binary);
        if (!file)
        {
            g_sceneSuppressSave = false;
            g_sceneDirty = false;
            return;
        }

        std::string header;
        int version = 0;
        file >> header >> version;
        if (header != "VENGINE_SCENE" || version != 1)
        {
            g_sceneSuppressSave = false;
            g_sceneDirty = false;
            return;
        }

        size_t cubeCount = 0;
        file >> cubeCount;
        file.ignore(std::numeric_limits<std::streamsize>::max(), '\n');

        for (size_t i = 0; i < cubeCount; ++i)
        {
            std::string line;
            if (!std::getline(file, line))
            {
                break;
            }
            if (line.empty())
            {
                continue;
            }

            std::istringstream iss(line);
            PlacedCube cube;
            int glowing = 0;
            int transparent = 0;
            iss >> cube.gridX >> cube.gridY >> cube.gridZ >> cube.r >> cube.g >> cube.b >> glowing >> transparent >> cube.presetIndex;
            if (!iss)
            {
                continue;
            }
            cube.glowing = glowing != 0;
            cube.transparent = transparent != 0;
            if (!(iss >> std::quoted(cube.texturePath)))
            {
                cube.texturePath.clear();
            }
            if (!cube.texturePath.empty())
            {
                const std::string absolute = MakeAbsoluteTexturePath(cube.texturePath);
                std::string loadStatus;
                const int handle = LoadTextureFromFile(absolute, loadStatus);
                cube.textureHandle = handle >= 0 ? handle : kInvalidTextureHandle;
            }
            else
            {
                cube.textureHandle = kInvalidTextureHandle;
            }
            g_placedCubes.push_back(std::move(cube));
        }

        g_sceneSuppressSave = false;
        g_sceneDirty = false;
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
            float u;
            float v;
        };

        constexpr float s = 0.5f;

        constexpr VertexData vertices[] = {
            // Front
            {-s, -s, s, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f},
            {s, -s, s, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f},
            {s, s, s, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f},
            {-s, -s, s, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f},
            {s, s, s, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f},
            {-s, s, s, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f},

            // Right
            {s, -s, s, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f},
            {s, -s, -s, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f},
            {s, s, -s, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f},
            {s, -s, s, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f},
            {s, s, -s, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f},
            {s, s, s, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f},

            // Back
            {s, -s, -s, 0.0f, 0.0f, -1.0f, 1.0f, 0.0f},
            {-s, -s, -s, 0.0f, 0.0f, -1.0f, 0.0f, 0.0f},
            {-s, s, -s, 0.0f, 0.0f, -1.0f, 0.0f, 1.0f},
            {s, -s, -s, 0.0f, 0.0f, -1.0f, 1.0f, 0.0f},
            {-s, s, -s, 0.0f, 0.0f, -1.0f, 0.0f, 1.0f},
            {s, s, -s, 0.0f, 0.0f, -1.0f, 1.0f, 1.0f},

            // Left
            {-s, -s, -s, -1.0f, 0.0f, 0.0f, 1.0f, 0.0f},
            {-s, -s, s, -1.0f, 0.0f, 0.0f, 0.0f, 0.0f},
            {-s, s, s, -1.0f, 0.0f, 0.0f, 0.0f, 1.0f},
            {-s, -s, -s, -1.0f, 0.0f, 0.0f, 1.0f, 0.0f},
            {-s, s, s, -1.0f, 0.0f, 0.0f, 0.0f, 1.0f},
            {-s, s, -s, -1.0f, 0.0f, 0.0f, 1.0f, 1.0f},

            // Top
            {-s, s, s, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f},
            {s, s, s, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f},
            {s, s, -s, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f},
            {-s, s, s, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f},
            {s, s, -s, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f},
            {-s, s, -s, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f},

            // Bottom
            {-s, -s, -s, 0.0f, -1.0f, 0.0f, 0.0f, 1.0f},
            {s, -s, -s, 0.0f, -1.0f, 0.0f, 1.0f, 1.0f},
            {s, -s, s, 0.0f, -1.0f, 0.0f, 1.0f, 0.0f},
            {-s, -s, -s, 0.0f, -1.0f, 0.0f, 0.0f, 1.0f},
            {s, -s, s, 0.0f, -1.0f, 0.0f, 1.0f, 0.0f},
            {-s, -s, s, 0.0f, -1.0f, 0.0f, 0.0f, 0.0f},
        };

        Mesh mesh;
        const size_t vertexCount = sizeof(vertices) / sizeof(vertices[0]);
        mesh.vertices.reserve(vertexCount * 3);
        mesh.normals.reserve(vertexCount * 3);
        mesh.texcoords.reserve(vertexCount * 2);

        for (size_t i = 0; i < vertexCount; ++i)
        {
            const VertexData& v = vertices[i];
            mesh.vertices.push_back(v.px);
            mesh.vertices.push_back(v.py);
            mesh.vertices.push_back(v.pz);
            mesh.normals.push_back(v.nx);
            mesh.normals.push_back(v.ny);
            mesh.normals.push_back(v.nz);
            mesh.texcoords.push_back(v.u);
            mesh.texcoords.push_back(v.v);
        }

        return mesh;
    }

    void RenderGround()
    {
        glDisable(GL_LIGHTING);
        const float cellSize = kGridCellSize;
        const float shadowBase[3] = {0.08f, 0.08f, 0.09f};
        const float litBase[3] = {0.28f, 0.29f, 0.32f};

        glBegin(GL_QUADS);
        for (int x = -kGridHalfSize; x < kGridHalfSize; ++x)
        {
            const float cellMinX = static_cast<float>(x) * cellSize;
            const float cellMaxX = cellMinX + cellSize;
            const float sampleX = cellMinX + cellSize * 0.5f;

            for (int z = -kGridHalfSize; z < kGridHalfSize; ++z)
            {
                const float cellMinZ = static_cast<float>(z) * cellSize;
                const float cellMaxZ = cellMinZ + cellSize;
                const float sampleZ = cellMinZ + cellSize * 0.5f;

                Vec3 samplePoint{sampleX, 0.05f, sampleZ};
                const float light = ComputeLightAtPoint(samplePoint, nullptr);
                const float r = shadowBase[0] + (litBase[0] - shadowBase[0]) * light;
                const float g = shadowBase[1] + (litBase[1] - shadowBase[1]) * light;
                const float b = shadowBase[2] + (litBase[2] - shadowBase[2]) * light;
                glColor3f(r, g, b);

                glVertex3f(cellMinX, 0.0f, cellMinZ);
                glVertex3f(cellMaxX, 0.0f, cellMinZ);
                glVertex3f(cellMaxX, 0.0f, cellMaxZ);
                glVertex3f(cellMinX, 0.0f, cellMaxZ);
            }
        }
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

    void RenderMesh(const Mesh& mesh, float r = 0.6f, float g = 0.7f, float b = 1.0f, float a = 1.0f, int textureHandle = kInvalidTextureHandle)
    {
        const LoadedTexture* texture = GetTextureInfo(textureHandle);
        const bool textureEnabled = texture && texture->id != 0;
        if (textureEnabled)
        {
            glEnable(GL_TEXTURE_2D);
            glBindTexture(GL_TEXTURE_2D, texture->id);
            glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
        }

        glColor4f(r, g, b, a);
        glBegin(GL_TRIANGLES);
        const size_t count = mesh.vertices.size();
        const bool hasTexcoords = mesh.texcoords.size() >= (count / 3) * 2;
        for (size_t i = 0; i < count; i += 3)
        {
            const size_t texIndex = (i / 3) * 2;
            if (textureEnabled && hasTexcoords && texIndex + 1 < mesh.texcoords.size())
            {
                glTexCoord2f(mesh.texcoords[texIndex], mesh.texcoords[texIndex + 1]);
            }
            glNormal3f(mesh.normals[i], mesh.normals[i + 1], mesh.normals[i + 2]);
            glVertex3f(mesh.vertices[i], mesh.vertices[i + 1], mesh.vertices[i + 2]);
        }
        glEnd();

        if (textureEnabled)
        {
            glBindTexture(GL_TEXTURE_2D, 0);
            glDisable(GL_TEXTURE_2D);
        }
    }

    void RenderGlowAura(const PlacedCube& cube)
    {
        glPushAttrib(GL_ENABLE_BIT | GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glDisable(GL_LIGHTING);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE);
        glDepthMask(GL_FALSE);

        glPushMatrix();
        glTranslatef(static_cast<float>(cube.gridX), static_cast<float>(cube.gridY) + 0.5f, static_cast<float>(cube.gridZ));

        constexpr int kSegments = 32;
        const float radius = 1.8f;
        auto drawDisk = [&](float angle, float axisX, float axisY, float axisZ) {
            glPushMatrix();
            if (angle != 0.0f)
            {
                glRotatef(angle, axisX, axisY, axisZ);
            }
            glBegin(GL_TRIANGLE_FAN);
            glColor4f(cube.r, cube.g, cube.b, 0.45f);
            glVertex3f(0.0f, 0.0f, 0.0f);
            glColor4f(cube.r, cube.g, cube.b, 0.0f);
            for (int i = 0; i <= kSegments; ++i)
            {
                const float theta = (static_cast<float>(i) / static_cast<float>(kSegments)) * 2.0f * kPi;
                const float px = std::cos(theta) * radius;
                const float py = std::sin(theta) * radius;
                glVertex3f(px, py, 0.0f);
            }
            glEnd();
            glPopMatrix();
        };

        drawDisk(0.0f, 1.0f, 0.0f, 0.0f);   // XY plane
        drawDisk(90.0f, 1.0f, 0.0f, 0.0f);  // XZ plane
        drawDisk(90.0f, 0.0f, 1.0f, 0.0f);  // YZ plane

        glPopMatrix();

        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);
        glPopAttrib();
    }

    void RenderTransparentCubes(const Mesh& mesh, const std::vector<const PlacedCube*>& cubes)
    {
        if (cubes.empty())
        {
            return;
        }

        glPushAttrib(GL_ENABLE_BIT | GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(GL_FALSE);

        constexpr float kAlpha = 0.45f;
        const GLfloat kNoEmission[] = {0.0f, 0.0f, 0.0f, 1.0f};

        for (const PlacedCube* cube : cubes)
        {
            glPushMatrix();
            glTranslatef(static_cast<float>(cube->gridX), static_cast<float>(cube->gridY) + 0.5f, static_cast<float>(cube->gridZ));
            Vec3 samplePos{static_cast<float>(cube->gridX), static_cast<float>(cube->gridY) + 0.5f, static_cast<float>(cube->gridZ)};
            const float lightAmount = cube->glowing ? 1.0f : ComputeLightAtPoint(samplePos, cube);
            const float shading = std::clamp(0.5f + 0.5f * lightAmount, 0.2f, 1.2f);
            const float tintedR = std::clamp(cube->r * shading, 0.0f, 1.0f);
            const float tintedG = std::clamp(cube->g * shading, 0.0f, 1.0f);
            const float tintedB = std::clamp(cube->b * shading, 0.0f, 1.0f);
            if (cube->glowing)
            {
                const GLfloat emission[] = {cube->r * 0.4f, cube->g * 0.4f, cube->b * 0.4f, 1.0f};
                glMaterialfv(GL_FRONT_AND_BACK, GL_EMISSION, emission);
            }
            RenderMesh(mesh, tintedR, tintedG, tintedB, kAlpha, cube->textureHandle);
            if (cube->glowing)
            {
                glMaterialfv(GL_FRONT_AND_BACK, GL_EMISSION, kNoEmission);
            }
            glPopMatrix();
        }

        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);
        glPopAttrib();

        for (const PlacedCube* cube : cubes)
        {
            if (cube->glowing)
            {
                RenderGlowAura(*cube);
            }
        }
    }

    void RenderDraggingCubePreview(const Mesh& mesh)
    {
        if (!g_draggingCube)
        {
            return;
        }

        int drawX = g_dragPreviewHasPosition ? g_dragPreviewX : g_draggedCube.gridX;
        int drawY = g_dragPreviewHasPosition ? g_dragPreviewY : g_draggedCube.gridY;
        int drawZ = g_dragPreviewHasPosition ? g_dragPreviewZ : g_draggedCube.gridZ;

        Vec3 samplePos{static_cast<float>(drawX), static_cast<float>(drawY) + 0.5f, static_cast<float>(drawZ)};
        const float lightAmount = g_draggedCube.glowing ? 1.0f : ComputeLightAtPoint(samplePos, &g_draggedCube);
        const float shading = std::clamp(0.4f + 0.6f * lightAmount, 0.2f, 1.0f);
        const float shadedR = std::clamp(g_draggedCube.r * shading, 0.0f, 1.0f);
        const float shadedG = std::clamp(g_draggedCube.g * shading, 0.0f, 1.0f);
        const float shadedB = std::clamp(g_draggedCube.b * shading, 0.0f, 1.0f);
        const float alpha = g_draggedCube.transparent ? 0.45f : 1.0f;

        glPushAttrib(GL_ENABLE_BIT | GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_POLYGON_BIT);
        if (g_draggedCube.transparent)
        {
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glDepthMask(GL_FALSE);
        }
        glPushMatrix();
        glTranslatef(static_cast<float>(drawX), static_cast<float>(drawY) + 0.5f, static_cast<float>(drawZ));
        const GLfloat kNoEmission[] = {0.0f, 0.0f, 0.0f, 1.0f};
        if (g_draggedCube.glowing)
        {
            const GLfloat emission[] = {g_draggedCube.r * 0.6f, g_draggedCube.g * 0.6f, g_draggedCube.b * 0.6f, 1.0f};
            glMaterialfv(GL_FRONT_AND_BACK, GL_EMISSION, emission);
        }
        RenderMesh(mesh, shadedR, shadedG, shadedB, alpha, g_draggedCube.textureHandle);
        if (g_draggedCube.glowing)
        {
            glMaterialfv(GL_FRONT_AND_BACK, GL_EMISSION, kNoEmission);
        }
        glPopMatrix();
        if (g_draggedCube.transparent)
        {
            glDepthMask(GL_TRUE);
            glDisable(GL_BLEND);
        }

        glDisable(GL_LIGHTING);
        glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
        glLineWidth(2.0f);
        const float highlightIntensity = g_dragPreviewValid ? 1.0f : 0.5f;
        glColor3f(0.8f * highlightIntensity, 0.45f * highlightIntensity, 1.0f * highlightIntensity);
        glPushMatrix();
        glTranslatef(static_cast<float>(drawX), static_cast<float>(drawY) + 0.5f, static_cast<float>(drawZ));
        RenderMesh(mesh);
        glPopMatrix();
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        glEnable(GL_LIGHTING);
        glPopAttrib();

        if (g_draggedCube.glowing && g_dragPreviewHasPosition)
        {
            PlacedCube previewGlow = g_draggedCube;
            previewGlow.gridX = drawX;
            previewGlow.gridY = drawY;
            previewGlow.gridZ = drawZ;
            RenderGlowAura(previewGlow);
        }
    }

    bool IsLightOccluded(const Vec3& origin, const Vec3& target, const PlacedCube* lightCube, const PlacedCube* receiver)
    {
        Vec3 dir = target - origin;
        const float dirLengthSq = dir.x * dir.x + dir.y * dir.y + dir.z * dir.z;
        if (dirLengthSq < 1e-6f)
        {
            return false;
        }

        for (const PlacedCube& cube : g_placedCubes)
        {
            if (&cube == lightCube || &cube == receiver)
            {
                continue;
            }
            if (cube.transparent)
            {
                continue;
            }

            Vec3 minB{static_cast<float>(cube.gridX) - 0.5f, static_cast<float>(cube.gridY), static_cast<float>(cube.gridZ) - 0.5f};
            Vec3 maxB{static_cast<float>(cube.gridX) + 0.5f, static_cast<float>(cube.gridY) + 1.0f, static_cast<float>(cube.gridZ) + 0.5f};
            float t = 0.0f;
            Vec3 normal;
            if (RayIntersectsAABB(origin, dir, minB, maxB, t, normal))
            {
                if (t > 1e-4f && t < 1.0f)
                {
                    return true;
                }
            }
        }

        return false;
    }

    float ComputeLightAtPoint(const Vec3& point, const PlacedCube* receiver)
    {
        float total = 0.2f;
        bool hasGlow = false;

        for (const PlacedCube& glow : g_placedCubes)
        {
            if (!glow.glowing)
            {
                continue;
            }

            hasGlow = true;
            Vec3 lightPos{
                static_cast<float>(glow.gridX),
                static_cast<float>(glow.gridY) + 0.5f,
                static_cast<float>(glow.gridZ)};

            if (IsLightOccluded(lightPos, point, &glow, receiver))
            {
                continue;
            }

            Vec3 delta = point - lightPos;
            const float distSq = delta.x * delta.x + delta.y * delta.y + delta.z * delta.z;
            if (distSq < 1e-4f)
            {
                total += 1.0f;
                continue;
            }

            constexpr float kIntensity = 2.6f;
            constexpr float kFalloff = 0.45f;
            const float contribution = kIntensity / (1.0f + distSq * kFalloff);
            total += contribution;
        }

        if (!hasGlow)
        {
            return 0.35f;
        }

        return std::clamp(total, 0.0f, 1.0f);
    }

    void RenderPlacedCubes(const Mesh& mesh)
    {
        const GLfloat kNoEmission[] = {0.0f, 0.0f, 0.0f, 1.0f};
        std::vector<const PlacedCube*> transparentCubes;
        for (const PlacedCube& cube : g_placedCubes)
        {
            if (cube.transparent)
            {
                transparentCubes.push_back(&cube);
                continue;
            }
            glPushMatrix();
            glTranslatef(static_cast<float>(cube.gridX), static_cast<float>(cube.gridY) + 0.5f, static_cast<float>(cube.gridZ));
            Vec3 samplePos{static_cast<float>(cube.gridX), static_cast<float>(cube.gridY) + 0.5f, static_cast<float>(cube.gridZ)};
            const float lightAmount = cube.glowing ? 1.0f : ComputeLightAtPoint(samplePos, &cube);
            const float shading = std::clamp(0.4f + 0.6f * lightAmount, 0.2f, 1.0f);
            const float shadedR = std::clamp(cube.r * shading, 0.0f, 1.0f);
            const float shadedG = std::clamp(cube.g * shading, 0.0f, 1.0f);
            const float shadedB = std::clamp(cube.b * shading, 0.0f, 1.0f);
            if (cube.glowing)
            {
                const GLfloat emission[] = {cube.r * 0.6f, cube.g * 0.6f, cube.b * 0.6f, 1.0f};
                glMaterialfv(GL_FRONT_AND_BACK, GL_EMISSION, emission);
            }
            RenderMesh(mesh, shadedR, shadedG, shadedB, 1.0f, cube.textureHandle);
            if (cube.glowing)
            {
                glMaterialfv(GL_FRONT_AND_BACK, GL_EMISSION, kNoEmission);
            }
            glPopMatrix();

            if (cube.glowing)
            {
                RenderGlowAura(cube);
            }
        }

        if (!transparentCubes.empty())
        {
            RenderTransparentCubes(mesh, transparentCubes);
        }

        RenderDraggingCubePreview(mesh);
    }

    bool OverlapsRange(float minA, float maxA, float minB, float maxB)
    {
        return maxA > minB && minA < maxB;
    }

    bool CollidesAtPosition(const Vec3& pos)
    {
        const float minX = pos.x - kPlayerRadius;
        const float maxX = pos.x + kPlayerRadius;
        const float minY = pos.y;
        const float maxY = pos.y + kPlayerHeight;
        const float minZ = pos.z - kPlayerRadius;
        const float maxZ = pos.z + kPlayerRadius;

        for (const PlacedCube& cube : g_placedCubes)
        {
            const float cubeMinX = static_cast<float>(cube.gridX) - 0.5f;
            const float cubeMaxX = static_cast<float>(cube.gridX) + 0.5f;
            const float cubeMinY = static_cast<float>(cube.gridY);
            const float cubeMaxY = static_cast<float>(cube.gridY) + 1.0f;
            const float cubeMinZ = static_cast<float>(cube.gridZ) - 0.5f;
            const float cubeMaxZ = static_cast<float>(cube.gridZ) + 0.5f;

            if (OverlapsRange(minX, maxX, cubeMinX, cubeMaxX) &&
                OverlapsRange(minY, maxY, cubeMinY, cubeMaxY) &&
                OverlapsRange(minZ, maxZ, cubeMinZ, cubeMaxZ))
            {
                return true;
            }
        }

        return false;
    }

    float HighestSurfaceAt(const Vec3& pos)
    {
        float height = 0.0f;
        const float minX = pos.x - kPlayerRadius;
        const float maxX = pos.x + kPlayerRadius;
        const float minZ = pos.z - kPlayerRadius;
        const float maxZ = pos.z + kPlayerRadius;

        for (const PlacedCube& cube : g_placedCubes)
        {
            const float cubeMinX = static_cast<float>(cube.gridX) - 0.5f;
            const float cubeMaxX = static_cast<float>(cube.gridX) + 0.5f;
            const float cubeMinZ = static_cast<float>(cube.gridZ) - 0.5f;
            const float cubeMaxZ = static_cast<float>(cube.gridZ) + 0.5f;
            if (OverlapsRange(minX, maxX, cubeMinX, cubeMaxX) &&
                OverlapsRange(minZ, maxZ, cubeMinZ, cubeMaxZ))
            {
                height = std::max(height, static_cast<float>(cube.gridY) + 1.0f);
            }
        }

        return height;
    }

    void UpdatePlayerMovement(float deltaTime)
    {
        Vec3 position{g_game.cubeX, g_game.cubeY, g_game.cubeZ};

        Vec3 moveInput{0.0f, 0.0f, 0.0f};
        if (g_moveForward)
        {
            Vec3 forward = CameraForward2D();
            moveInput.x += forward.x;
            moveInput.z += forward.z;
        }
        if (g_moveBackward)
        {
            Vec3 forward = CameraForward2D();
            moveInput.x -= forward.x;
            moveInput.z -= forward.z;
        }
        if (g_moveLeft)
        {
            Vec3 right = CameraRight2D();
            moveInput.x -= right.x;
            moveInput.z -= right.z;
        }
        if (g_moveRight)
        {
            Vec3 right = CameraRight2D();
            moveInput.x += right.x;
            moveInput.z += right.z;
        }

        const float length = std::sqrt(moveInput.x * moveInput.x + moveInput.z * moveInput.z);
        if (length > 0.0f)
        {
            moveInput.x /= length;
            moveInput.z /= length;
        }

        constexpr float kMoveSpeed = 4.0f;
        g_game.velX = moveInput.x * kMoveSpeed;
        g_game.velZ = moveInput.z * kMoveSpeed;

        if (std::fabs(g_game.velX) > 0.001f || std::fabs(g_game.velZ) > 0.001f)
        {
            g_game.rotation = std::fmod((std::atan2(g_game.velX, g_game.velZ) * 180.0f / kPi) + 360.0f, 360.0f);
        }

        auto tryStep = [&](Vec3& attempt) -> bool {
            const float currentHeight = HighestSurfaceAt(position);
            const float targetHeight = HighestSurfaceAt(attempt);
            if (targetHeight > currentHeight + 0.01f && targetHeight - currentHeight <= kStepHeight + 0.01f)
            {
                Vec3 stepped = attempt;
                stepped.y = targetHeight;
                if (!CollidesAtPosition(stepped))
                {
                    position = stepped;
                    g_game.cubeVelocity = 0.0f;
                    g_game.grounded = true;
                    return true;
                }
            }
            return false;
        };

        auto moveHorizontal = [&](float deltaX, float deltaZ) {
            if (deltaX == 0.0f && deltaZ == 0.0f)
            {
                return;
            }

            Vec3 attempt = position;
            attempt.x += deltaX;
            attempt.z += deltaZ;

            if (CollidesAtPosition(attempt))
            {
                if (g_game.grounded && tryStep(attempt))
                {
                    return;
                }
                if (deltaX != 0.0f)
                {
                    g_game.velX = 0.0f;
                }
                if (deltaZ != 0.0f)
                {
                    g_game.velZ = 0.0f;
                }
            }
            else
            {
                position = attempt;
            }
        };

        moveHorizontal(g_game.velX * deltaTime, 0.0f);
        moveHorizontal(0.0f, g_game.velZ * deltaTime);

        const float gravity = -9.8f;
        g_game.cubeVelocity += gravity * deltaTime;
        if (g_jumpRequested && g_game.grounded)
        {
            g_game.cubeVelocity = 5.2f;
            g_game.grounded = false;
        }
        g_jumpRequested = false;

        Vec3 verticalAttempt = position;
        verticalAttempt.y += g_game.cubeVelocity * deltaTime;
        if (verticalAttempt.y < 0.0f)
        {
            verticalAttempt.y = 0.0f;
            g_game.cubeVelocity = 0.0f;
            g_game.grounded = true;
        }

        if (CollidesAtPosition(verticalAttempt))
        {
            if (g_game.cubeVelocity < 0.0f)
            {
                verticalAttempt.y = HighestSurfaceAt(position);
                g_game.grounded = true;
            }
            else
            {
                g_game.cubeVelocity = 0.0f;
                verticalAttempt.y = position.y;
            }
            g_game.cubeVelocity = 0.0f;
        }
        else
        {
            g_game.grounded = verticalAttempt.y <= HighestSurfaceAt(verticalAttempt) + 0.01f;
        }

        position = verticalAttempt;

        g_game.cubeX = position.x;
        g_game.cubeY = position.y;
        g_game.cubeZ = position.z;
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
        glColor3f(g_gradientTop.r, g_gradientTop.g, g_gradientTop.b);
        glVertex2f(-1.0f, 1.0f);
        glVertex2f(1.0f, 1.0f);
        glColor3f(g_gradientBottom.r, g_gradientBottom.g, g_gradientBottom.b);
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
        const float pitchRadians = g_cameraPitchDegrees * (kPi / 180.0f);
        const float cameraHeight = std::sin(pitchRadians) * g_cameraDistance;
        const float cameraForward = std::cos(pitchRadians) * g_cameraDistance;
        const float yawRadians = g_cameraYawDegrees * (kPi / 180.0f);
        const float dirX = std::sin(yawRadians);
        const float dirZ = std::cos(yawRadians);
        const float eyeX = g_cameraFocusX + dirX * cameraForward;
        const float eyeY = cameraHeight;
        const float eyeZ = g_cameraFocusZ + dirZ * cameraForward;
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
        const float playerLight = ComputeLightAtPoint(Vec3{g_game.cubeX, g_game.cubeY + 0.5f, g_game.cubeZ}, nullptr);
        const float playerShade = std::clamp(0.5f + 0.5f * playerLight, 0.3f, 1.0f);
        RenderMesh(mesh, 0.6f * playerShade, 0.7f * playerShade, 1.0f * playerShade, 1.0f, kInvalidTextureHandle);
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
            case 'W':
                g_moveForward = true;
                return 0;
            case VK_DOWN:
            case 'S':
                g_moveBackward = true;
                return 0;
            case VK_LEFT:
            case 'A':
                g_moveLeft = true;
                return 0;
            case VK_RIGHT:
            case 'D':
                g_moveRight = true;
                return 0;
            case 'Q':
                g_cameraYawTarget -= 90.0f;
                NormalizeAngle(g_cameraYawTarget);
                return 0;
            case 'E':
                    g_cameraYawTarget += 90.0f;
                    NormalizeAngle(g_cameraYawTarget);
                    return 0;
            case 'R':
                g_cameraPitchTarget = std::min(g_cameraPitchTarget + kCameraPitchStepDegrees, kCameraPitchMaxDegrees);
                return 0;
            case 'F':
                g_cameraPitchTarget = std::max(g_cameraPitchTarget - kCameraPitchStepDegrees, kCameraPitchMinDegrees);
                return 0;
            case 'C':
                g_showContentPanel = !g_showContentPanel;
                return 0;
            default:
                break;
            }
            }
            break;
        case WM_KEYUP:
            switch (wParam)
            {
            case VK_UP:
            case 'W':
                g_moveForward = false;
                return 0;
            case VK_DOWN:
            case 'S':
                g_moveBackward = false;
                return 0;
            case VK_LEFT:
            case 'A':
                g_moveLeft = false;
                return 0;
            case VK_RIGHT:
            case 'D':
                g_moveRight = false;
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
        {
            if (ImGui::GetIO().WantCaptureMouse)
            {
                break;
            }

            const int mouseX = GET_X_LPARAM(lParam);
            const int mouseY = GET_Y_LPARAM(lParam);
            Vec3 origin;
            Vec3 direction;
            if (!ComputeRayFromScreen(mouseX, mouseY, origin, direction))
            {
                break;
            }

            RayHit hit = CastWorldRay(origin, direction);
            if (!hit.hit)
            {
                break;
            }

            if (hit.hitCube)
            {
                int cubeIndex = FindCubeIndex(hit.cubeX, hit.cubeY, hit.cubeZ);
                if (cubeIndex >= 0)
                {
                    g_pendingCubeDrag = true;
                    g_pendingDragIndex = cubeIndex;
                    g_pendingDragStartPos = POINT{mouseX, mouseY};
                    g_pendingDragStartTime = GetSeconds();
                    g_pendingDragHit = hit;
                    g_pendingPlacementPresetIndex = g_selectedPresetIndex;
                }
            }
            else
            {
                int targetX = 0;
                int targetY = 0;
                int targetZ = 0;
                if (ComputePlacementTarget(hit, targetX, targetY, targetZ))
                {
                    const int presetIndex = std::clamp(g_selectedPresetIndex, 0, static_cast<int>(kSpawnPresetCount) - 1);
                    const SpawnPreset& preset = kSpawnPresets[presetIndex];
                    const int textureHandle = g_presetTextureHandles[presetIndex];
                    const std::string texturePath = (textureHandle >= 0) ? g_presetTexturePaths[presetIndex] : std::string();
                    PlaceCube(targetX, targetY, targetZ, preset, presetIndex, textureHandle, texturePath);
                    if (CollidesAtPosition(Vec3{g_game.cubeX, g_game.cubeY, g_game.cubeZ}))
                    {
                        float top = HighestSurfaceAt(Vec3{g_game.cubeX, g_game.cubeY, g_game.cubeZ});
                        g_game.cubeY = top;
                        g_game.cubeVelocity = 0.0f;
                        g_game.grounded = true;
                    }
                }
            }
            return 0;
        }
        case WM_LBUTTONUP:
        {
            if (g_draggingCube)
            {
                FinishCubeDrag(g_dragPreviewValid && g_dragPreviewHasPosition);
                return 0;
            }
            if (g_pendingCubeDrag)
            {
                int targetX = 0;
                int targetY = 0;
                int targetZ = 0;
                if (ComputePlacementTarget(g_pendingDragHit, targetX, targetY, targetZ))
                {
                    int presetIndex = g_pendingPlacementPresetIndex;
                    if (presetIndex < 0)
                    {
                        presetIndex = g_selectedPresetIndex;
                    }
                    presetIndex = std::clamp(presetIndex, 0, static_cast<int>(kSpawnPresetCount) - 1);
                    const SpawnPreset& preset = kSpawnPresets[presetIndex];
                    const int textureHandle = g_presetTextureHandles[presetIndex];
                    const std::string texturePath = (textureHandle >= 0) ? g_presetTexturePaths[presetIndex] : std::string();
                    PlaceCube(targetX, targetY, targetZ, preset, presetIndex, textureHandle, texturePath);
                    if (CollidesAtPosition(Vec3{g_game.cubeX, g_game.cubeY, g_game.cubeZ}))
                    {
                        float top = HighestSurfaceAt(Vec3{g_game.cubeX, g_game.cubeY, g_game.cubeZ});
                        g_game.cubeY = top;
                        g_game.cubeVelocity = 0.0f;
                        g_game.grounded = true;
                    }
                }
                CancelPendingCubeDrag();
                return 0;
            }
            break;
        }
        case WM_MOUSEMOVE:
        {
            const int mouseX = GET_X_LPARAM(lParam);
            const int mouseY = GET_Y_LPARAM(lParam);
            if (g_draggingCube)
            {
                if (wParam & MK_LBUTTON)
                {
                    UpdateDraggingCubePreview(mouseX, mouseY);
                }
                else
                {
                    FinishCubeDrag(false);
                }
                return 0;
            }

            if (g_pendingCubeDrag)
            {
                if (wParam & MK_LBUTTON)
                {
                    double elapsed = GetSeconds() - g_pendingDragStartTime;
                    const int deltaX = std::abs(mouseX - g_pendingDragStartPos.x);
                    const int deltaY = std::abs(mouseY - g_pendingDragStartPos.y);
                    if (elapsed >= kDragStartHoldSeconds ||
                        deltaX >= kDragStartPixelThreshold ||
                        deltaY >= kDragStartPixelThreshold)
                    {
                        BeginCubeDrag(g_pendingDragIndex, mouseX, mouseY);
                        g_pendingCubeDrag = false;
                        if (g_draggingCube)
                        {
                            SetCapture(hwnd);
                        }
                    }
                }
                else
                {
                    CancelPendingCubeDrag();
                }
            }
            break;
        }
        case WM_RBUTTONDOWN:
        {
            if (g_draggingCube)
            {
                FinishCubeDrag(false);
            }
            CancelPendingCubeDrag();

            if (ImGui::GetIO().WantCaptureMouse)
            {
                break;
            }

            const int mouseX = GET_X_LPARAM(lParam);
            const int mouseY = GET_Y_LPARAM(lParam);
            Vec3 origin;
            Vec3 direction;
            if (!ComputeRayFromScreen(mouseX, mouseY, origin, direction))
            {
                break;
            }

            RayHit hit = CastWorldRay(origin, direction);
            if (!hit.hit)
            {
                break;
            }

            if (hit.hitCube)
            {
                RemoveCube(hit.cubeX, hit.cubeY, hit.cubeZ);
            }
            else if (hit.hitGround)
            {
                int topIndex = FindHighestCubeIndex(hit.groundX, hit.groundZ);
                if (topIndex >= 0)
                {
                    const PlacedCube& cube = g_placedCubes[topIndex];
                    RemoveCube(cube.gridX, cube.gridY, cube.gridZ);
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
        "GYGE",
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
    g_sceneFilePath = BuildSceneFilePath();
    LoadSceneFromFile();
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
        ImGui::SameLine();
        if (ImGui::Button(g_showEnvironmentPanel ? "Env On" : "Env"))
        {
            g_showEnvironmentPanel = !g_showEnvironmentPanel;
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
                if (preset.glowing)
                {
                    ImGui::SameLine();
                    ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f), "glow");
                }
                if (preset.transparent)
                {
                    ImGui::SameLine();
                    ImGui::TextColored(ImVec4(0.6f, 0.85f, 1.0f, 1.0f), "glass");
                }
                ImGui::NewLine();
                ImGui::Indent();
                ImGui::PushItemWidth(240.0f);
                ImGui::InputTextWithHint("##TexturePath", "texture.png", &g_presetTexturePaths[i]);
                ImGui::PopItemWidth();
                ImGui::SameLine();
                if (ImGui::Button("Load PNG"))
                {
                    std::string relative;
                    std::string normalizeStatus;
                    if (NormalizeTextureInputPath(g_presetTexturePaths[i], relative, normalizeStatus))
                    {
                        const std::string absolute = MakeAbsoluteTexturePath(relative);
                        std::string loadStatus;
                        const int handle = LoadTextureFromFile(absolute, loadStatus);
                        if (handle >= 0)
                        {
                            g_presetTextureHandles[i] = handle;
                            g_presetTexturePaths[i] = relative;
                        }
                        g_presetTextureStatus[i] = loadStatus;
                    }
                    else
                    {
                        g_presetTextureStatus[i] = normalizeStatus;
                    }
                }
                ImGui::SameLine();
                if (g_presetTextureHandles[i] >= 0 && ImGui::Button("Clear"))
                {
                    g_presetTextureHandles[i] = kInvalidTextureHandle;
                    g_presetTexturePaths[i].clear();
                    g_presetTextureStatus[i].clear();
                }
                if (!g_presetTextureStatus[i].empty())
                {
                    const bool ok = g_presetTextureHandles[i] >= 0 && g_presetTextureStatus[i] == "Loaded";
                    const ImVec4 statusColor = ok ? ImVec4(0.4f, 0.85f, 0.5f, 1.0f) : ImVec4(0.95f, 0.45f, 0.45f, 1.0f);
                    ImGui::TextColored(statusColor, "%s", g_presetTextureStatus[i].c_str());
                }
                if (g_presetTextureHandles[i] >= 0)
                {
                    if (const LoadedTexture* tex = GetTextureInfo(g_presetTextureHandles[i]))
                    {
                        ImGui::TextColored(ImVec4(0.7f, 0.85f, 1.0f, 1.0f), "%d x %d", tex->width, tex->height);
                    }
                }
                ImGui::Unindent();
                ImGui::Spacing();
                ImGui::Separator();
                ImGui::PopID();
            }

            ImGui::End();
        }

        if (g_showEnvironmentPanel)
        {
            ImGui::SetNextWindowPos(ImVec2(static_cast<float>(g_windowWidth) - 260.0f, 60.0f), ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(240.0f, 160.0f), ImGuiCond_Always);
            ImGuiWindowFlags envFlags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings;
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 6.0f);
            ImGui::Begin("EnvironmentPanel", nullptr, envFlags);
            ImGui::PopStyleVar();

            ImGui::TextUnformatted("Background Gradient");
            ImGui::Separator();
            bool topChanged = ImGui::ColorEdit3("Top", &g_gradientTop.r, ImGuiColorEditFlags_NoInputs);
            bool bottomChanged = ImGui::ColorEdit3("Bottom", &g_gradientBottom.r, ImGuiColorEditFlags_NoInputs);
            if (topChanged || bottomChanged)
            {
                g_gradientTop.r = std::clamp(g_gradientTop.r, 0.0f, 1.0f);
                g_gradientTop.g = std::clamp(g_gradientTop.g, 0.0f, 1.0f);
                g_gradientTop.b = std::clamp(g_gradientTop.b, 0.0f, 1.0f);
                g_gradientBottom.r = std::clamp(g_gradientBottom.r, 0.0f, 1.0f);
                g_gradientBottom.g = std::clamp(g_gradientBottom.g, 0.0f, 1.0f);
                g_gradientBottom.b = std::clamp(g_gradientBottom.b, 0.0f, 1.0f);
            }
            ImGui::Spacing();
            if (ImGui::Button("Reset##Env"))
            {
                g_gradientTop = {0.18f, 0.13f, 0.25f};
                g_gradientBottom = {0.03f, 0.05f, 0.12f};
            }

            ImGui::End();
        }

        UpdatePlayerMovement(deltaTime);

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
        const float yawDifference = ShortestAngleDelta(g_cameraYawDegrees, g_cameraYawTarget);
        const float maxDelta = kCameraRotationSpeed * deltaTime;
        const float clampedDelta = std::clamp(yawDifference, -maxDelta, maxDelta);
        g_cameraYawDegrees += clampedDelta;
        const float pitchDifference = std::clamp(g_cameraPitchTarget, kCameraPitchMinDegrees, kCameraPitchMaxDegrees) - g_cameraPitchDegrees;
        const float maxPitchDelta = kCameraPitchSpeed * deltaTime;
        const float clampedPitch = std::clamp(pitchDifference, -maxPitchDelta, maxPitchDelta);
        g_cameraPitchDegrees = std::clamp(g_cameraPitchDegrees + clampedPitch, kCameraPitchMinDegrees, kCameraPitchMaxDegrees);
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
    if (g_sceneDirty)
    {
        SaveSceneToFile();
    }

    CleanupLoadedTextures();
    ShutdownGdiplus();

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
