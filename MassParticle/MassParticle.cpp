// Example low level rendering Unity plugin


#include "UnityPluginInterface.h"

#include <math.h>
#include <stdio.h>
#include <tbb/tbb.h>
#include "mpTypes.h"
#include "MassParticle.h"

mpWorld g_mpWorld;


// --------------------------------------------------------------------------
// Include headers for the graphics APIs we support

#if SUPPORT_D3D9
    #include <d3d9.h>
#endif
#if SUPPORT_D3D11
    #include <d3d11.h>
#endif
#if SUPPORT_OPENGL
    #if UNITY_WIN
        #include <gl/GL.h>
    #else
        #include <OpenGL/OpenGL.h>
    #endif
#endif



// --------------------------------------------------------------------------
// Helper utilities


// Prints a string
static void DebugLog (const char* str)
{
    #if UNITY_WIN
    OutputDebugStringA (str);
    #else
    printf ("%s", str);
    #endif
}

// COM-like Release macro
#ifndef SAFE_RELEASE
#define SAFE_RELEASE(a) if (a) { a->Release(); a = NULL; }
#endif



// --------------------------------------------------------------------------
// SetTimeFromUnity, an example function we export which is called by one of the scripts.

static float g_Time;

extern "C" void EXPORT_API SetTimeFromUnity (float t) { g_Time = t; }



// --------------------------------------------------------------------------
// SetTextureFromUnity, an example function we export which is called by one of the scripts.

static void* g_TexturePointer;

extern "C" void EXPORT_API SetTextureFromUnity (void* texturePtr)
{
    // A script calls this at initialization time; just remember the texture pointer here.
    // Will update texture pixels each frame from the plugin rendering event (texture update
    // needs to happen on the rendering thread).
    g_TexturePointer = texturePtr;
}



// --------------------------------------------------------------------------
// UnitySetGraphicsDevice

static int g_DeviceType = -1;


// Actual setup/teardown functions defined below
#if SUPPORT_D3D9
static void SetGraphicsDeviceD3D9 (IDirect3DDevice9* device, GfxDeviceEventType eventType);
#endif
#if SUPPORT_D3D11
static void SetGraphicsDeviceD3D11 (ID3D11Device* device, GfxDeviceEventType eventType);
#endif

extern "C" void EXPORT_API UnitySetGraphicsDevice (void* device, int deviceType, int eventType)
{
    g_DeviceType = -1;
    
    #if SUPPORT_D3D9
    if (deviceType == kGfxRendererD3D9)
    {
        g_DeviceType = deviceType;
    }
#endif

    #if SUPPORT_D3D11
    if (deviceType == kGfxRendererD3D11)
    {
        if (eventType == kGfxDeviceEventInitialize) {
            g_DeviceType = deviceType;
            g_mpWorld.m_renderer = mpCreateRendererD3D11(device, g_mpWorld);
        }
        else if (eventType == kGfxDeviceEventShutdown) {
            delete g_mpWorld.m_renderer;
            g_mpWorld.m_renderer = nullptr;
        }
        else if (eventType == kGfxDeviceEventBeforeReset) {
            mpClearParticles();
        }
    }
    #endif

    #if SUPPORT_OPENGL
    if (deviceType == kGfxRendererOpenGL)
    {
        g_DeviceType = deviceType;
    }
    #endif
}



extern "C" void EXPORT_API UnityRenderEvent (int eventID)
{
    if (g_mpWorld.m_renderer) {
        g_mpWorld.m_renderer->render();
    }
}



bool mpInitialize(ID3D11Device *dev)
{
    tbb::task_scheduler_init tbb_init;

    g_mpWorld.m_renderer = mpCreateRendererD3D11(dev, g_mpWorld);
    return g_mpWorld.m_renderer !=nullptr;
}

void mpFinalize()
{
    delete g_mpWorld.m_renderer;
    g_mpWorld.m_renderer = nullptr;
}

void mpClearParticles()
{
    g_mpWorld.clearParticles();
}

extern "C" EXPORT_API void mpReloadShader()
{
    if (g_mpWorld.m_renderer) {
        g_mpWorld.m_renderer->reloadShader();
    }
}


extern "C" EXPORT_API ispc::KernelParams mpGetKernelParams()
{
    return g_mpWorld.m_params;
}

extern "C" EXPORT_API void mpSetKernelParams(ispc::KernelParams *params)
{
    g_mpWorld.m_params = *(mpKernelParams*)params;
}

extern "C" EXPORT_API void mpSetViewProjectionMatrix(XMFLOAT4X4 view, XMFLOAT4X4 proj)
{
    g_mpWorld.m_camera.forceSetMatrix(view, proj);
}

extern "C" EXPORT_API uint32_t mpGetNumParticles()
{
    return g_mpWorld.m_num_active_particles;
}
extern "C" EXPORT_API mpParticle* mpGetParticles()
{
    return g_mpWorld.particles;
}

extern "C" EXPORT_API int32_t mpScatterParticlesSphererical(XMFLOAT3 center, float radius, int32_t num)
{
    if (num <= 0) { return 0; }

    std::vector<mpParticle> particles(num);
    for (size_t i = 0; i < particles.size(); ++i) {
        particles[i].position = ist::simdvec4_set(
            center.x + mpGenRand()*radius, center.y + mpGenRand()*radius, center.z + mpGenRand()*radius, 1.0f);
        particles[i].velocity = ist::simdvec4_set(mpGenRand()*0.4f, mpGenRand()*0.4f, mpGenRand()*0.4f, 0.0f);
    }
    g_mpWorld.addParticles(&particles[0], particles.size());
    return num;
}


inline float sign(float v)
{
    return v < 0.0f ? -1.0f : 1.0f;
}

extern "C" EXPORT_API uint32_t mpAddBoxCollider(int32_t owner, XMFLOAT4X4 transform, XMFLOAT3 size)
{
    size.x = size.x * 0.5f;
    size.y = size.y * 0.5f;
    size.z = size.z * 0.5f;

    XMMATRIX st = XMMATRIX((float*)&transform);
    XMVECTOR vertices[] = {
        { size.x, size.y, size.z, 0.0f },
        { -size.x, size.y, size.z, 0.0f },
        { -size.x, -size.y, size.z, 0.0f },
        { size.x, -size.y, size.z, 0.0f },
        { size.x, size.y, -size.z, 0.0f },
        { -size.x, size.y, -size.z, 0.0f },
        { -size.x, -size.y, -size.z, 0.0f },
        { size.x, -size.y, -size.z, 0.0f },
    };
    for (int i = 0; i < _countof(vertices); ++i) {
        vertices[i] = XMVector4Transform(vertices[i], st);
    }

    XMVECTOR normals[6] = {
        XMVector3Normalize(XMVector3Cross(XMVectorSubtract(vertices[3], vertices[0]), XMVectorSubtract(vertices[4], vertices[0]))),
        XMVector3Normalize(XMVector3Cross(XMVectorSubtract(vertices[5], vertices[1]), XMVectorSubtract(vertices[2], vertices[1]))),
        XMVector3Normalize(XMVector3Cross(XMVectorSubtract(vertices[7], vertices[3]), XMVectorSubtract(vertices[2], vertices[3]))),
        XMVector3Normalize(XMVector3Cross(XMVectorSubtract(vertices[1], vertices[0]), XMVectorSubtract(vertices[4], vertices[0]))),
        XMVector3Normalize(XMVector3Cross(XMVectorSubtract(vertices[1], vertices[0]), XMVectorSubtract(vertices[3], vertices[0]))),
        XMVector3Normalize(XMVector3Cross(XMVectorSubtract(vertices[7], vertices[4]), XMVectorSubtract(vertices[5], vertices[4]))),
    };
    float32 distances[6] = {
        -(XMVector3Dot(vertices[0], normals[0]).m128_f32[0] + mpParticleSize),
        -(XMVector3Dot(vertices[1], normals[1]).m128_f32[0] + mpParticleSize),
        -(XMVector3Dot(vertices[0], normals[2]).m128_f32[0] + mpParticleSize),
        -(XMVector3Dot(vertices[3], normals[3]).m128_f32[0] + mpParticleSize),
        -(XMVector3Dot(vertices[0], normals[4]).m128_f32[0] + mpParticleSize),
        -(XMVector3Dot(vertices[4], normals[5]).m128_f32[0] + mpParticleSize),
    };

    ispc::BoxCollider o = {
        uint32_t(owner),
        { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f },
        transform.m[3][0], transform.m[3][1], transform.m[3][2],
        {
            { normals[0].m128_f32[0], normals[0].m128_f32[1], normals[0].m128_f32[2], distances[0] },
            { normals[1].m128_f32[0], normals[1].m128_f32[1], normals[1].m128_f32[2], distances[1] },
            { normals[2].m128_f32[0], normals[2].m128_f32[1], normals[2].m128_f32[2], distances[2] },
            { normals[3].m128_f32[0], normals[3].m128_f32[1], normals[3].m128_f32[2], distances[3] },
            { normals[4].m128_f32[0], normals[4].m128_f32[1], normals[4].m128_f32[2], distances[4] },
            { normals[5].m128_f32[0], normals[5].m128_f32[1], normals[5].m128_f32[2], distances[5] },
        }
    };

    for (int32 i = 0; i < _countof(vertices); ++i) {
        float x = o.x + vertices[i].m128_f32[0];
        float y = o.y + vertices[i].m128_f32[1];
        float z = o.z + vertices[i].m128_f32[2];
        if (i==0) {
            o.bb.bl_x = o.bb.ur_x = x;
            o.bb.bl_y = o.bb.ur_y = y;
            o.bb.bl_z = o.bb.ur_z = z;
        }
        o.bb.bl_x = std::min<float>(o.bb.bl_x, x-mpParticleSize);
        o.bb.bl_y = std::min<float>(o.bb.bl_y, y-mpParticleSize);
        o.bb.bl_z = std::min<float>(o.bb.bl_z, z-mpParticleSize);
        o.bb.ur_x = std::max<float>(o.bb.ur_x, x+mpParticleSize);
        o.bb.ur_y = std::max<float>(o.bb.ur_y, y+mpParticleSize);
        o.bb.ur_z = std::max<float>(o.bb.ur_z, z+mpParticleSize);
    }

    g_mpWorld.collision_boxes.push_back(o);
    return 0;
}

extern "C" EXPORT_API uint32_t mpAddSphereCollider(int32_t owner, XMFLOAT3 center, float radius)
{
    float er = radius + mpParticleSize;
    ispc::SphereCollider sphere = {
        owner,
        { center.x-er, center.y-er, center.z-er,
          center.x+er, center.y+er, center.z+er },
        center.x, center.y, center.z, radius + mpParticleSize
    };
    g_mpWorld.collision_spheres.push_back(sphere);
    return 0;
}

extern "C" EXPORT_API uint32_t mpAddDirectionalForce(XMFLOAT3 direction, float strength)
{
    ispc::DirectionalForce force;
    set_nxyz(force, direction.x, direction.y, direction.z);
    force.strength = strength;
    g_mpWorld.force_directional.push_back(force);
    return 0;
}


extern "C" EXPORT_API void mpUpdate(float dt)
{
    static mpPerformanceCounter s_timer;
    static float s_prev = 0.0f;
    mpPerformanceCounter timer;

    {
        std::unique_lock<std::mutex> lock(g_mpWorld.m_mutex);
        g_mpWorld.update(1.0f);
        g_mpWorld.clearCollidersAndForces();
    }

    if (s_timer.getElapsedMillisecond() - s_prev > 1000.0f) {
        char buf[128];
        _snprintf(buf, _countof(buf), "  SPH update: %d particles %.3fms\n", g_mpWorld.m_num_active_particles, timer.getElapsedMillisecond());
        OutputDebugStringA(buf);
        s_prev = s_timer.getElapsedMillisecond();
    }
}