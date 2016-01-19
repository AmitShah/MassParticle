#include "UnityPluginInterface.h"

#if SUPPORT_D3D9

#include <windows.h>
#include <d3d9.h>
#include <map>
#include "mpTypes.h"
#include "mpCore_ispc.h"
#include "MassParticle.h"

#define mpSafeRelease(obj) if(obj) { obj->Release(); obj=nullptr; }


class mpRendererD3D9 : public mpRenderer
{
public:
    mpRendererD3D9(void *dev);
    virtual ~mpRendererD3D9();
    virtual void updateDataTexture(void *tex, int width, int height, const void *data, size_t data_size);

private:
    IDirect3DSurface9* findOrCreateStagingTexture(int width, int height);

private:
    IDirect3DDevice9 *m_device;
    std::map<uint64_t, IDirect3DSurface9*> m_staging_textures;
};

mpRenderer* mpCreateRendererD3D9(void *device)
{
    return new mpRendererD3D9(device);
}


mpRendererD3D9::mpRendererD3D9(void *dev)
: m_device((IDirect3DDevice9*)dev)
{
}

mpRendererD3D9::~mpRendererD3D9()
{
    for (auto& pair : m_staging_textures)
    {
        pair.second->Release();
    }
    m_staging_textures.clear();
}

IDirect3DSurface9* mpRendererD3D9::findOrCreateStagingTexture(int width, int height)
{
    D3DFORMAT internal_format = D3DFMT_A32B32G32R32F;

    uint64_t hash = width + (height << 16);
    {
        auto it = m_staging_textures.find(hash);
        if (it != m_staging_textures.end())
        {
            return it->second;
        }
    }

    IDirect3DSurface9 *ret = nullptr;
    HRESULT hr = m_device->CreateOffscreenPlainSurface(width, height, internal_format, D3DPOOL_SYSTEMMEM, &ret, NULL);
    if (SUCCEEDED(hr))
    {
        m_staging_textures.insert(std::make_pair(hash, ret));
    }
    return ret;
}

void mpRendererD3D9::updateDataTexture(void *texptr, int width, int height, const void *data, size_t data_size)
{
    int psize = 16;

    HRESULT hr;
    IDirect3DTexture9 *tex = (IDirect3DTexture9*)texptr;

    // D3D11 �ƈႢ�AD3D9 �ł͏������݂� staging texture ���o�R����K�v������B
    IDirect3DSurface9 *surf_src = findOrCreateStagingTexture(mpDataTextureWidth, height);
    if (surf_src == nullptr) { return; }

    IDirect3DSurface9* surf_dst = nullptr;
    hr = tex->GetSurfaceLevel(0, &surf_dst);
    if (FAILED(hr)) { return; }

    bool ret = false;
    D3DLOCKED_RECT locked;
    hr = surf_src->LockRect(&locked, nullptr, D3DLOCK_DISCARD);
    if (SUCCEEDED(hr))
    {
        const char *rpixels = (const char*)data;
        int rpitch = psize * width;
        char *wpixels = (char*)locked.pBits;
        int wpitch = locked.Pitch;

        memcpy(wpixels, rpixels, data_size);
        surf_src->UnlockRect();

        hr = m_device->UpdateSurface(surf_src, nullptr, surf_dst, nullptr);
        if (SUCCEEDED(hr)) {
            ret = true;
        }
    }
    surf_dst->Release();
}
#endif // SUPPORT_D3D9
