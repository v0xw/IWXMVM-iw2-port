#include "StdInclude.hpp"
#include "Sky.hpp"

#include "../Addresses.hpp"
#include "../Structures.hpp"

namespace IWXMVM::IW2::Hooks::Sky
{
    namespace
    {
        constexpr int MAPTYPE_CUBE = 5;

        std::string overrideName;

        // Per-world application state. Pointers into renderer memory are only compared or written
        // back while the world they belong to is still current, never dereferenced after it's gone.
        std::string appliedName;
        Structures::GfxWorld* appliedWorld = nullptr;
        Structures::MaterialTextureDef* mapSkyTexdef = nullptr;
        Structures::GfxImage* originalImage = nullptr;
        uint8_t originalSamplerState = 0;
        Structures::GfxImage* overrideImage = nullptr;
        uint8_t overrideSamplerState = 0;

        std::string lastWarnedMaterial;

        void ResetPerWorldState()
        {
            appliedName.clear();
            appliedWorld = nullptr;
            mapSkyTexdef = nullptr;
            originalImage = nullptr;
            originalSamplerState = 0;
            overrideImage = nullptr;
            overrideSamplerState = 0;
        }

        Structures::MaterialTextureDef* FindColorMap(Structures::Material* material)
        {
            if (material == nullptr || material->textureTable == nullptr)
            {
                return nullptr;
            }

            for (uint16_t i = 0; i < material->textureCount; i++)
            {
                auto& textureDef = material->textureTable[i];
                if (textureDef.name != nullptr && _stricmp(textureDef.name, "colorMap") == 0)
                {
                    return &textureDef;
                }
            }
            return nullptr;
        }

        // The current map's sky material holds the world's sky image as its colorMap; find it in
        // the renderer's material hash table so we can patch the texdef the sky surfaces sample
        Structures::MaterialTextureDef* FindMapSkyTexdef(Structures::GfxImage* skyImage)
        {
            const auto table =
                reinterpret_cast<Structures::Material**>(Addresses::Gfx(Addresses::GfxRVA::materialHashTable));
            if (table == nullptr || skyImage == nullptr)
            {
                return nullptr;
            }

            for (int i = 0; i < 1024; i++)
            {
                const auto texdef = FindColorMap(table[i]);
                if (texdef != nullptr && texdef->image == skyImage)
                {
                    LOG_DEBUG("Sky override: map sky material is '{}'", table[i]->name);
                    return texdef;
                }
            }

            LOG_WARN("Sky override: no material references the world's sky image; patching only the world fields");
            return nullptr;
        }

        Structures::GfxImage* LoadSkyCubemap(const std::string& materialName, uint8_t& samplerState)
        {
            const auto registerHandle = reinterpret_cast<Structures::Material*(__cdecl*)(const char*, int, int)>(
                Addresses::Gfx(Addresses::GfxRVA::Material_RegisterHandle));
            if (registerHandle == nullptr)
            {
                return nullptr;
            }

            // usage 9 = what the world loader passes for BSP surface materials. A missing material
            // comes back as a duplicate of '$default', whose colorMap is not a cubemap, so it is
            // rejected below rather than breaking the sky.
            const auto material = registerHandle(materialName.c_str(), 0, 9);
            const auto texdef = FindColorMap(material);
            if (texdef == nullptr || texdef->image == nullptr || texdef->image->mapType != MAPTYPE_CUBE)
            {
                if (lastWarnedMaterial != materialName)
                {
                    lastWarnedMaterial = materialName;
                    LOG_WARN("Sky override: material '{}' has no cubemap colorMap; keeping the current sky",
                             materialName);
                }
                return nullptr;
            }

            samplerState = texdef->samplerState;
            return texdef->image;
        }
    }  // namespace

    void SetOverride(const std::string& materialName)
    {
        overrideName = materialName;
        lastWarnedMaterial.clear();
    }

    void Apply()
    {
        const auto worldAddress = Addresses::Gfx(Addresses::GfxRVA::rgp_world);
        if (worldAddress == 0)
        {
            return;
        }

        const auto world = *reinterpret_cast<Structures::GfxWorld**>(worldAddress);
        if (world == nullptr || world != appliedWorld)
        {
            // no world, or a different one: everything we tracked belongs to a world that is gone
            ResetPerWorldState();
        }
        if (world == nullptr)
        {
            return;
        }

        if (overrideName.empty())
        {
            if (appliedWorld == world && overrideImage != nullptr)
            {
                if (mapSkyTexdef != nullptr && mapSkyTexdef->image == overrideImage)
                {
                    mapSkyTexdef->image = originalImage;
                    mapSkyTexdef->samplerState = originalSamplerState;
                }
                if (world->skyImage == overrideImage)
                {
                    world->skyImage = originalImage;
                    world->skySamplerState = originalSamplerState;
                }
                LOG_DEBUG("Sky override: restored the map's own sky");
            }
            ResetPerWorldState();
            return;
        }

        // steady state: our override is in place and unchanged
        if (appliedWorld == world && appliedName == overrideName && overrideImage != nullptr &&
            world->skyImage == overrideImage &&
            (mapSkyTexdef == nullptr || mapSkyTexdef->image == overrideImage))
        {
            return;
        }

        if (appliedWorld == world && appliedName == overrideName && overrideImage != nullptr)
        {
            // something re-derived the sky from the original data; note it once, then re-apply below
            LOG_DEBUG("Sky override: engine reset the sky (world: {}, texdef: {}); re-applying",
                      world->skyImage != overrideImage, mapSkyTexdef != nullptr && mapSkyTexdef->image != overrideImage);
        }

        if (appliedWorld != world)
        {
            // first application in this world: remember its own sky and locate its sky material
            // before we touch anything
            originalImage = world->skyImage;
            originalSamplerState = world->skySamplerState;
            mapSkyTexdef = FindMapSkyTexdef(originalImage);
            appliedWorld = world;
        }

        uint8_t samplerState = 0;
        const auto image = LoadSkyCubemap(overrideName, samplerState);
        if (image == nullptr)
        {
            return;
        }

        if (mapSkyTexdef != nullptr)
        {
            mapSkyTexdef->image = image;
            mapSkyTexdef->samplerState = samplerState;
        }
        world->skyImage = image;
        world->skySamplerState = samplerState;
        overrideImage = image;
        overrideSamplerState = samplerState;

        if (appliedName != overrideName)
        {
            LOG_INFO("Sky override: applied '{}' (image {}, original {})", overrideName,
                     reinterpret_cast<void*>(image), reinterpret_cast<void*>(originalImage));
        }
        appliedName = overrideName;
    }
}  // namespace IWXMVM::IW2::Hooks::Sky
