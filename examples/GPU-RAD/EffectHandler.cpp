#include "EffectHandler.h"
#include "EffectCB.h"
#include "EffectShaders.h"

#include <string>
#include <iostream>
#include <fstream>

using namespace irr;
using namespace scene;
using namespace video;
using namespace core;


EffectHandler::EffectHandler(IrrlichtDevice* dev, const irr::core::dimension2du& screenRTTSize,
                             const bool useVSMShadows, const bool useRoundSpotLights, const bool use32BitDepthBuffers)
    : device(dev), smgr(dev->getSceneManager()), driver(dev->getVideoDriver()),
    ScreenRTTSize(screenRTTSize.getArea() == 0 ? dev->getVideoDriver()->getScreenSize() : screenRTTSize),
    ClearColour(0x0), shadowsUnsupported(false), DepthRTT(0), DepthPass(false), depthMC(0), shadowMC(0),
    AmbientColour(0x0), use32BitDepth(use32BitDepthBuffers), useVSM(useVSMShadows)
{
    bool tempTexFlagMipMaps = driver->getTextureCreationFlag(ETCF_CREATE_MIP_MAPS);
    bool tempTexFlag32 = driver->getTextureCreationFlag(ETCF_ALWAYS_32_BIT);

    ScreenRTT = driver->addRenderTargetTexture(ScreenRTTSize,"a",video::ECF_A8R8G8B8);
    ScreenQuad.rt[0] = driver->addRenderTargetTexture(ScreenRTTSize,"b",video::ECF_A8R8G8B8);
    ScreenQuad.rt[1] = driver->addRenderTargetTexture(ScreenRTTSize,"c",video::ECF_A8R8G8B8);


    driver->setTextureCreationFlag(ETCF_CREATE_MIP_MAPS, tempTexFlagMipMaps);
    driver->setTextureCreationFlag(ETCF_ALWAYS_32_BIT, tempTexFlag32);

    CShaderPreprocessor sPP(driver);

    E_SHADER_EXTENSION shaderExt = (driver->getDriverType() == EDT_DIRECT3D9) ? ESE_HLSL : ESE_GLSL;

    video::IGPUProgrammingServices* gpu = driver->getGPUProgrammingServices();

    if (gpu && ((driver->getDriverType() == EDT_OPENGL && driver->queryFeature(EVDF_ARB_GLSL)) ||
                (driver->getDriverType() == EDT_DIRECT3D9 && driver->queryFeature(EVDF_PIXEL_SHADER_2_0))))
    {
        depthMC = new DepthShaderCB(this);
        shadowMC = new ShadowShaderCB(this);

        Depth = gpu->addHighLevelShaderMaterial(
                    sPP.ppShader(SHADOW_PASS_1V[shaderExt]).c_str(), "vertexMain", video::EVST_VS_2_0,
                    sPP.ppShader(SHADOW_PASS_1P[shaderExt]).c_str(), "pixelMain", video::EPST_PS_2_0,
                    depthMC, video::EMT_SOLID);

        DepthT = gpu->addHighLevelShaderMaterial(
                     sPP.ppShader(SHADOW_PASS_1V[shaderExt]).c_str(), "vertexMain", video::EVST_VS_2_0,
                     sPP.ppShader(SHADOW_PASS_1PT[shaderExt]).c_str(), "pixelMain", video::EPST_PS_2_0,
                     depthMC, video::EMT_TRANSPARENT_ALPHA_CHANNEL_REF);

        WhiteWash = gpu->addHighLevelShaderMaterial(
                        sPP.ppShader(SHADOW_PASS_1V[shaderExt]).c_str(), "vertexMain", video::EVST_VS_2_0,
                        sPP.ppShader(WHITE_WASH_P[shaderExt]).c_str(), "pixelMain", video::EPST_PS_2_0,
                        depthMC, video::EMT_SOLID);

        WhiteWashTRef = gpu->addHighLevelShaderMaterial(
                            sPP.ppShader(SHADOW_PASS_1V[shaderExt]).c_str(), "vertexMain", video::EVST_VS_2_0,
                            sPP.ppShader(WHITE_WASH_P[shaderExt]).c_str(), "pixelMain", video::EPST_PS_2_0,
                            depthMC, video::EMT_TRANSPARENT_ALPHA_CHANNEL_REF);

        WhiteWashTAdd = gpu->addHighLevelShaderMaterial(
                            sPP.ppShader(SHADOW_PASS_1V[shaderExt]).c_str(), "vertexMain", video::EVST_VS_2_0,
                            sPP.ppShader(WHITE_WASH_P_ADD[shaderExt]).c_str(), "pixelMain", video::EPST_PS_2_0,
                            depthMC, video::EMT_TRANSPARENT_ALPHA_CHANNEL);

        WhiteWashTAlpha = gpu->addHighLevelShaderMaterial(
                              sPP.ppShader(SHADOW_PASS_1V[shaderExt]).c_str(), "vertexMain", video::EVST_VS_2_0,
                              sPP.ppShader(WHITE_WASH_P[shaderExt]).c_str(), "pixelMain", video::EPST_PS_2_0,
                              depthMC, video::EMT_TRANSPARENT_ALPHA_CHANNEL);

        if (useRoundSpotLights)
            sPP.addShaderDefine("ROUND_SPOTLIGHTS");

        if (useVSMShadows)
            sPP.addShaderDefine("VSM");

        const u32 sampleCounts[EFT_COUNT] = {1, 4, 8, 12, 16};

        const E_VERTEX_SHADER_TYPE vertexProfile =
            driver->queryFeature(video::EVDF_VERTEX_SHADER_3_0) ? EVST_VS_3_0 : EVST_VS_2_0;

        const E_PIXEL_SHADER_TYPE pixelProfile =
            driver->queryFeature(video::EVDF_PIXEL_SHADER_3_0) ? EPST_PS_3_0 : EPST_PS_2_0;

        for (u32 i = 0; i < EFT_COUNT; i++)
        {
            sPP.addShaderDefine("SAMPLE_AMOUNT", core::stringc(sampleCounts[i]));
            Shadow[i] = gpu->addHighLevelShaderMaterial(
                            sPP.ppShader(SHADOW_PASS_2V[shaderExt]).c_str(), "vertexMain", vertexProfile,
                            sPP.ppShader(SHADOW_PASS_2P[shaderExt]).c_str(), "pixelMain", pixelProfile,
                            shadowMC, video::EMT_SOLID);
        }

        for (u32 i = 0; i < EFT_COUNT; i++)
        {
            //sPP.addShaderDefine("SAMPLE_AMOUNT", core::stringc(sampleCounts[i]));
            ShadowSS[i] = gpu->addHighLevelShaderMaterial(
                              sPP.ppShader(SHADOW_PASS_2VS[shaderExt]).c_str(), "vertexMain", vertexProfile,
                              sPP.ppShader(SHADOW_PASS_2P[shaderExt]).c_str(), "pixelMain", pixelProfile,
                              shadowMC, video::EMT_SOLID);
        }
        sPP.addShaderDefine("ROUND_SPOTLIGHTS");
        for (u32 i = 0; i < EFT_COUNT; i++)
        {
            //sPP.addShaderDefine("SAMPLE_AMOUNT", core::stringc(sampleCounts[i]));
            ShadowSSS[i] = gpu->addHighLevelShaderMaterial(
                               sPP.ppShader(SHADOW_PASS_2VS[shaderExt]).c_str(), "vertexMain", vertexProfile,
                               sPP.ppShader(SHADOW_PASS_2P[shaderExt]).c_str(), "pixelMain", pixelProfile,
                               shadowMC, video::EMT_SOLID);
        }

        // Set resolution preprocessor defines.
        sPP.addShaderDefine("SCREENX", core::stringc(ScreenRTTSize.Width));
        sPP.addShaderDefine("SCREENY", core::stringc(ScreenRTTSize.Height));

        // Create screen quad shader callback.
        ScreenQuadCB* SQCB = new ScreenQuadCB(this, true);

        // Light modulate.
        LightModulate = gpu->addHighLevelShaderMaterial(
                            sPP.ppShader(SCREEN_QUAD_V[shaderExt]).c_str(), "vertexMain", vertexProfile,
                            sPP.ppShader(LIGHT_MODULATE_P[shaderExt]).c_str(), "pixelMain", pixelProfile, SQCB);

        // Simple present.
        Simple = gpu->addHighLevelShaderMaterial(
                     sPP.ppShader(SCREEN_QUAD_V[shaderExt]).c_str(), "vertexMain", vertexProfile,
                     sPP.ppShader(SIMPLE_P[shaderExt]).c_str(), "pixelMain", pixelProfile, SQCB,
                     video::EMT_TRANSPARENT_ADD_COLOR);

        // VSM blur.
        VSMBlurH = gpu->addHighLevelShaderMaterial(
                       sPP.ppShader(SCREEN_QUAD_V[shaderExt]).c_str(), "vertexMain", vertexProfile,
                       sPP.ppShader(VSM_BLUR_P[shaderExt]).c_str(), "pixelMain", pixelProfile, SQCB,video::EMT_LIGHTMAP);

        sPP.addShaderDefine("VERTICAL_VSM_BLUR");

        VSMBlurV = gpu->addHighLevelShaderMaterial(
                       sPP.ppShader(SCREEN_QUAD_V[shaderExt]).c_str(), "vertexMain", vertexProfile,
                       sPP.ppShader(VSM_BLUR_P[shaderExt]).c_str(), "pixelMain", pixelProfile, SQCB,video::EMT_LIGHTMAP);

        // Drop the screen quad callback.
        SQCB->drop();
    }
    else
    {
        Depth = EMT_SOLID;
        DepthT = EMT_SOLID;
        WhiteWash = EMT_SOLID;
        WhiteWashTRef = EMT_TRANSPARENT_ALPHA_CHANNEL_REF;
        WhiteWashTAdd = EMT_TRANSPARENT_ADD_COLOR;
        WhiteWashTAlpha = EMT_TRANSPARENT_ALPHA_CHANNEL;
        Simple = EMT_SOLID;

        for (u32 i = 0; i < EFT_COUNT; ++i)
            Shadow[i] = EMT_SOLID;

        device->getLogger()->log("XEffects: Shader effects not supported on this system.");
        shadowsUnsupported = true;
    }
}


EffectHandler::~EffectHandler()
{
    if (ScreenRTT)
        driver->removeTexture(ScreenRTT);

    if (ScreenQuad.rt[0])
        driver->removeTexture(ScreenQuad.rt[0]);

    if (ScreenQuad.rt[1])
        driver->removeTexture(ScreenQuad.rt[1]);

    if (DepthRTT)
        driver->removeTexture(DepthRTT);
}


void EffectHandler::setScreenRenderTargetResolution(const irr::core::dimension2du& resolution)
{
    bool tempTexFlagMipMaps = driver->getTextureCreationFlag(ETCF_CREATE_MIP_MAPS);
    bool tempTexFlag32 = driver->getTextureCreationFlag(ETCF_ALWAYS_32_BIT);

    if (ScreenRTT)
        driver->removeTexture(ScreenRTT);

    ScreenRTT = driver->addRenderTargetTexture(resolution);

    if (ScreenQuad.rt[0])
        driver->removeTexture(ScreenQuad.rt[0]);

    ScreenQuad.rt[0] = driver->addRenderTargetTexture(resolution);

    if (ScreenQuad.rt[1])
        driver->removeTexture(ScreenQuad.rt[1]);

    ScreenQuad.rt[1] = driver->addRenderTargetTexture(resolution);

    if (DepthRTT != 0)
    {
        driver->removeTexture(DepthRTT);
        DepthRTT = driver->addRenderTargetTexture(resolution);
    }

    driver->setTextureCreationFlag(ETCF_CREATE_MIP_MAPS, tempTexFlagMipMaps);
    driver->setTextureCreationFlag(ETCF_ALWAYS_32_BIT, tempTexFlag32);

    ScreenRTTSize = resolution;
}


void EffectHandler::enableDepthPass(bool enableDepthPass)
{
    DepthPass = enableDepthPass;

    if (DepthPass && DepthRTT == 0)
        DepthRTT = driver->addRenderTargetTexture(ScreenRTTSize, "depthRTT", ECF_A8R8G8B8);
}


void EffectHandler::addPostProcessingEffect(irr::s32 MaterialType, IPostProcessingRenderCallback* callback)
{
    SPostProcessingPair pPair(MaterialType, 0);
    pPair.renderCallback = callback;
    PostProcessingRoutines.push_back(pPair);
}


void EffectHandler::addShadowToNode(irr::scene::ISceneNode *node, E_FILTER_TYPE filterType, E_SHADOW_MODE shadowMode)
{
    SShadowNode snode = {node, shadowMode, filterType};
    ShadowNodeArray.push_back(snode);
}


void EffectHandler::addNodeToDepthPass(irr::scene::ISceneNode *node)
{
    if (DepthPassArray.binary_search(node) == -1)
        DepthPassArray.push_back(node);
}


void EffectHandler::removeNodeFromDepthPass(irr::scene::ISceneNode *node)
{
    s32 i = DepthPassArray.binary_search(node);

    if (i != -1)
        DepthPassArray.erase(i);
}


void EffectHandler::update(const irr::u32 time, const bool screenSpaceOnly,irr::video::ITexture* outputTarget,irr::scene::ISceneNode* node,irr::core::array<scene::IMeshBuffer*>* buffers)
{
    if(outputTarget)
    {
        if(outputTarget->getSize()!= ScreenRTT->getSize())
        {
            setScreenRenderTargetResolution(outputTarget->getSize());
        }
    }
    if (shadowsUnsupported || smgr->getActiveCamera() == 0)
        return;

    if (!ShadowNodeArray.empty() && !LightList.empty())
    {
        driver->setRenderTarget(ScreenQuad.rt[0], true, true, AmbientColour);

        ICameraSceneNode* activeCam = smgr->getActiveCamera();
        activeCam->OnAnimate(device->getTimer()->getTime());
        activeCam->OnRegisterSceneNode();
        activeCam->render();

        const u32 ShadowNodeArraySize = ShadowNodeArray.size();
        const u32 LightListSize = LightList.size();
        for (u32 l = 0; l < LightListSize; ++l)
        {
            // Set max distance constant for depth shader.
            depthMC->FarLink = LightList[l].getFarValue();

            driver->setTransform(ETS_VIEW, LightList[l].getViewMatrix());
            driver->setTransform(ETS_PROJECTION, LightList[l].getProjectionMatrix());

            ITexture* currentShadowMapTexture = getShadowMapTexture(LightList[l].getShadowMapResolution());
            driver->setRenderTarget(currentShadowMapTexture, true, true, SColor(0xffffffff));

            for (u32 i = 0; i < ShadowNodeArraySize; ++i)
            {
                if (ShadowNodeArray[i].shadowMode == ESM_RECEIVE || ShadowNodeArray[i].shadowMode == ESM_EXCLUDE)
                    continue;

                const u32 CurrentMaterialCount = ShadowNodeArray[i].node->getMaterialCount();
                core::array<irr::s32> BufferMaterialList(CurrentMaterialCount);
                BufferMaterialList.set_used(0);

                for (u32 m = 0; m < CurrentMaterialCount; ++m)
                {
                    BufferMaterialList.push_back(ShadowNodeArray[i].node->getMaterial(m).MaterialType);
                    ShadowNodeArray[i].node->getMaterial(m).MaterialType = (E_MATERIAL_TYPE)
                            (BufferMaterialList[m] == video::EMT_TRANSPARENT_ALPHA_CHANNEL_REF ? DepthT : Depth);
                }

                ShadowNodeArray[i].node->OnAnimate(device->getTimer()->getTime());
                ShadowNodeArray[i].node->render();

                const u32 BufferMaterialListSize = BufferMaterialList.size();
                for (u32 m = 0; m < BufferMaterialListSize; ++m)
                    ShadowNodeArray[i].node->getMaterial(m).MaterialType = (E_MATERIAL_TYPE)BufferMaterialList[m];
            }

            // Blur the shadow map texture if we're using VSM filtering.
            if (useVSM)
            {
                ITexture* currentSecondaryShadowMap = getShadowMapTexture(LightList[l].getShadowMapResolution(), true);

                driver->setRenderTarget(currentSecondaryShadowMap, true, true, SColor(0xffffffff));
                ScreenQuad.getMaterial().setTexture(0, currentShadowMapTexture);
                ScreenQuad.getMaterial().setTexture(0, currentShadowMapTexture);
                ScreenQuad.getMaterial().MaterialType = (E_MATERIAL_TYPE)VSMBlurH;
                ScreenQuad.getMaterial().TextureLayer[0].TextureWrapU=1;
                ScreenQuad.getMaterial().TextureLayer[0].BilinearFilter=0;
                //ScreenQuad.getMaterial().TextureLayer[1].BilinearFilter=false;

                ScreenQuad.render(driver);

                driver->setRenderTarget(currentShadowMapTexture, true, true, SColor(0xffffffff));
                ScreenQuad.getMaterial().setTexture(0, currentSecondaryShadowMap);
                ScreenQuad.getMaterial().setTexture(0, currentSecondaryShadowMap);
                ScreenQuad.getMaterial().MaterialType = (E_MATERIAL_TYPE)VSMBlurV;
                ScreenQuad.getMaterial().TextureLayer[0].BilinearFilter=0;
                //ScreenQuad.getMaterial().TextureLayer[1].BilinearFilter=false;

                ScreenQuad.render(driver);
            }

            driver->setRenderTarget(ScreenQuad.rt[1], true, true, SColor(0,0,0,0));

            driver->setTransform(ETS_VIEW, activeCam->getViewMatrix());
            driver->setTransform(ETS_PROJECTION, activeCam->getProjectionMatrix());

            shadowMC->LightColour = LightList[l].getLightColor();
            shadowMC->LightLink = LightList[l].getPosition();
            shadowMC->FarLink = LightList[l].getFarValue();
            shadowMC->ViewLink = LightList[l].getViewMatrix();
            shadowMC->ProjLink = LightList[l].getProjectionMatrix();
            shadowMC->MapRes = (f32)LightList[l].getShadowMapResolution();

            for (u32 i = 0; i < ShadowNodeArraySize; ++i)
            {
                if (ShadowNodeArray[i].shadowMode == ESM_CAST || ShadowNodeArray[i].shadowMode == ESM_EXCLUDE)
                    continue;

                const u32 CurrentMaterialCount = ShadowNodeArray[i].node->getMaterialCount();
                core::array<irr::s32> BufferMaterialList(CurrentMaterialCount);
                core::array<irr::video::ITexture*> BufferTextureList(CurrentMaterialCount);

                core::array<u8> clampModesUList(CurrentMaterialCount);
                core::array<u8> clampModesVList(CurrentMaterialCount);



                for (u32 m = 0; m < CurrentMaterialCount; ++m)
                {
                    BufferMaterialList.push_back(ShadowNodeArray[i].node->getMaterial(m).MaterialType);
                    BufferTextureList.push_back(ShadowNodeArray[i].node->getMaterial(m).getTexture(0));

                    if (screenSpaceOnly)
                    {
                        if(LightList[l].isSpot())
                            ShadowNodeArray[i].node->getMaterial(m).MaterialType = (E_MATERIAL_TYPE)ShadowSSS[ShadowNodeArray[i].filterType];
                        else
                            ShadowNodeArray[i].node->getMaterial(m).MaterialType = (E_MATERIAL_TYPE)ShadowSS[ShadowNodeArray[i].filterType];
                    }

                    else
                        ShadowNodeArray[i].node->getMaterial(m).MaterialType = (E_MATERIAL_TYPE)Shadow[ShadowNodeArray[i].filterType];
                    ShadowNodeArray[i].node->getMaterial(m).setTexture(0, currentShadowMapTexture);

                    clampModesUList.push_back(ShadowNodeArray[i].node->getMaterial(m).TextureLayer[0].TextureWrapU);
                    clampModesVList.push_back(ShadowNodeArray[i].node->getMaterial(m).TextureLayer[0].TextureWrapV);
                    //ShadowNodeArray[i].node->getMaterial(m).TextureLayer[0].TextureWrapU=video::ETC_CLAMP;
                    //ShadowNodeArray[i].node->getMaterial(m).TextureLayer[0].TextureWrapV=video::ETC_CLAMP;
                }

                //Render all project shadow maps
                if (!node)
                {
                    ShadowNodeArray[i].node->OnAnimate(device->getTimer()->getTime());
                    ShadowNodeArray[i].node->render();
                }
                else
                {
                    //render projected shadows on specific buffers of a single shAdow map
                    if (ShadowNodeArray[i].node==node)
                    {
                        ShadowNodeArray[i].node->OnAnimate(device->getTimer()->getTime());

                        driver->setTransform(video::ETS_WORLD, node->getAbsoluteTransformation());
                        irr::core::array<scene::IMeshBuffer*>bufferlist=*buffers;

                        for (int buf=0; buf<bufferlist.size(); buf++)
                        {
                            driver->setMaterial(ShadowNodeArray[i].node->getMaterial(buf));
                            driver->drawMeshBuffer(bufferlist[buf]);
                        }
                    }
                }

                for (u32 m = 0; m < CurrentMaterialCount; ++m)
                {
                    ShadowNodeArray[i].node->getMaterial(m).MaterialType = (E_MATERIAL_TYPE)BufferMaterialList[m];
                    ShadowNodeArray[i].node->getMaterial(m).setTexture(0, BufferTextureList[m]);

                    ShadowNodeArray[i].node->getMaterial(m).TextureLayer[0].TextureWrapU=clampModesUList[m];
                    ShadowNodeArray[i].node->getMaterial(m).TextureLayer[0].TextureWrapV=clampModesVList[m];
                }
            }

            driver->setRenderTarget(ScreenQuad.rt[0], false, false, SColor(0,0,0,0));
            ScreenQuad.getMaterial().setTexture(0, ScreenQuad.rt[1]);
            ScreenQuad.getMaterial().MaterialType = (E_MATERIAL_TYPE)Simple;
            ScreenQuad.getMaterial().ColorMask=0xffffffff;
            ScreenQuad.render(driver);
        }
        /*
                // Render all the excluded and casting-only nodes.
                for (u32 i = 0;i < ShadowNodeArraySize;++i)
                {
                    if (ShadowNodeArray[i].shadowMode != ESM_CAST && ShadowNodeArray[i].shadowMode != ESM_EXCLUDE)
                        continue;

                    const u32 CurrentMaterialCount = ShadowNodeArray[i].node->getMaterialCount();
                    core::array<irr::s32> BufferMaterialList(CurrentMaterialCount);
                    BufferMaterialList.set_used(0);

                    for (u32 m = 0;m < CurrentMaterialCount;++m)
                    {
                        BufferMaterialList.push_back(ShadowNodeArray[i].node->getMaterial(m).MaterialType);

                        switch (BufferMaterialList[m])
                        {
                        case EMT_TRANSPARENT_ALPHA_CHANNEL_REF:
                            ShadowNodeArray[i].node->getMaterial(m).MaterialType = (E_MATERIAL_TYPE)WhiteWashTRef;
                            break;
                        case EMT_TRANSPARENT_ADD_COLOR:
                            ShadowNodeArray[i].node->getMaterial(m).MaterialType = (E_MATERIAL_TYPE)WhiteWashTAdd;
                            break;
                        case EMT_TRANSPARENT_ALPHA_CHANNEL:
                            ShadowNodeArray[i].node->getMaterial(m).MaterialType = (E_MATERIAL_TYPE)WhiteWashTAlpha;
                            break;
                        default:
                            ShadowNodeArray[i].node->getMaterial(m).MaterialType = (E_MATERIAL_TYPE)WhiteWash;
                            break;
                        }
                    }

                    ShadowNodeArray[i].node->OnAnimate(device->getTimer()->getTime());
                    ShadowNodeArray[i].node->render();

                    for (u32 m = 0;m < CurrentMaterialCount;++m)
                        ShadowNodeArray[i].node->getMaterial(m).MaterialType = (E_MATERIAL_TYPE)BufferMaterialList[m];
                }*/
    }
    else
    {
        driver->setRenderTarget(ScreenQuad.rt[0], true, true, SColor(0xffffffff));
    }
    if (!screenSpaceOnly)
    {
        driver->setRenderTarget(ScreenQuad.rt[1], true, true, SColor(0xffffffff));

        core::list<scene::ISceneNode*>::ConstIterator it = smgr->getRootSceneNode()->getChildren().begin();
        while (it!=smgr->getRootSceneNode()->getChildren().end())
        {
            scene::ISceneNode* node = *it;
            node->OnAnimate(time);
            node->render();
            it++;

        }
    }

    const u32 PostProcessingRoutinesSize = PostProcessingRoutines.size();

    if (!screenSpaceOnly)
    {
        driver->setRenderTarget(outputTarget, true, true, SColor(0x0));


        ScreenQuad.getMaterial().setTexture(0, ScreenQuad.rt[1]);
        ScreenQuad.getMaterial().setTexture(1, ScreenQuad.rt[0]);

        ScreenQuad.getMaterial().MaterialType = (E_MATERIAL_TYPE)LightModulate;

        ScreenQuad.render(driver);
    }

    if (outputTarget)
    {

        driver->setRenderTarget(ScreenRTT , true, true, SColor(0,255,0,255));
        ScreenQuad.getMaterial().setTexture(0, ScreenQuad.rt[0]);
        ScreenQuad.getMaterial().setTexture(1, ScreenQuad.rt[0]);
        ScreenQuad.getMaterial().MaterialType = (E_MATERIAL_TYPE)VSMBlurH;
        ScreenQuad.getMaterial().TextureLayer[0].TextureWrapU=1;
        ScreenQuad.getMaterial().TextureLayer[0].TextureWrapV=1;
        ScreenQuad.getMaterial().TextureLayer[0].BilinearFilter=true;
        ScreenQuad.getMaterial().TextureLayer[1].BilinearFilter=false;


        //ScreenQuad.getMaterial().setFlag(video::EMF_BILINEAR_FILTER,false);
        ScreenQuad.render(driver);

        driver->setRenderTarget(ScreenQuad.rt[0], true, true, SColor(0,255,0,255))  ;
        ScreenQuad.getMaterial().setTexture(0, ScreenRTT );
        ScreenQuad.getMaterial().setTexture(1, ScreenRTT );
        ScreenQuad.getMaterial().MaterialType = (E_MATERIAL_TYPE)VSMBlurV;
        ScreenQuad.getMaterial().TextureLayer[0].TextureWrapU=1;
        ScreenQuad.getMaterial().TextureLayer[0].TextureWrapV=1;
        ScreenQuad.getMaterial().TextureLayer[0].BilinearFilter=true;
        ScreenQuad.getMaterial().TextureLayer[1].BilinearFilter=false;

        ScreenQuad.render(driver);


        //ScreenQuad.getMaterial().setFlag(video::EMF_BILINEAR_FILTER,false);
        ScreenQuad.render(driver);

        driver->setRenderTarget(outputTarget, true, true, SColor(0,255,0,255))  ;
        ScreenQuad.getMaterial().setTexture(0, ScreenQuad.rt[0] );
        ScreenQuad.getMaterial().setTexture(1, ScreenQuad.rt[0] );
        ScreenQuad.getMaterial().MaterialType = (E_MATERIAL_TYPE)VSMBlurH;
        ScreenQuad.getMaterial().TextureLayer[0].TextureWrapU=1;
        ScreenQuad.getMaterial().TextureLayer[0].TextureWrapV=1;
        ScreenQuad.getMaterial().TextureLayer[0].BilinearFilter=true;
        ScreenQuad.getMaterial().TextureLayer[1].BilinearFilter=true;

        ScreenQuad.render(driver);




    }

}


irr::video::ITexture* EffectHandler::getShadowMapTexture(const irr::u32 resolution, const bool secondary)
{
    // Using Irrlicht cache now.
    core::stringc shadowMapName = core::stringc("XEFFECTS_SM_") + core::stringc(resolution);

    if (secondary)
        shadowMapName += "_2";

    ITexture* shadowMapTexture = driver->getTexture(shadowMapName);

    if (shadowMapTexture == 0)
    {
        device->getLogger()->log("XEffects: Please ignore previous warning, it is harmless.");

        shadowMapTexture = driver->addRenderTargetTexture(dimension2du(resolution, resolution),
                           shadowMapName, ECF_A8R8G8B8);
    }

    return shadowMapTexture;
}


irr::video::ITexture* EffectHandler::generateRandomVectorTexture(const irr::core::dimension2du& dimensions,
        const irr::core::stringc& name)
{
    IImage* tmpImage = driver->createImage(irr::video::ECF_A8R8G8B8, dimensions);

    srand(device->getTimer()->getRealTime());

    for (u32 x = 0; x < dimensions.Width; ++x)
    {
        for (u32 y = 0; y < dimensions.Height; ++y)
        {
            vector3df randVec;

            // Reject vectors outside the unit sphere to get a uniform distribution.
            do
            {
                randVec = vector3df((f32)rand() / (f32)RAND_MAX, (f32)rand() / (f32)RAND_MAX, (f32)rand() / (f32)RAND_MAX);
            }
            while (randVec.getLengthSQ() > 1.0f);

            const SColorf randCol(randVec.X, randVec.Y, randVec.Z);
            tmpImage->setPixel(x, y, randCol.toSColor());
        }
    }

    ITexture* randTexture = driver->addTexture(name, tmpImage);

    tmpImage->drop();

    return randTexture;
}


EffectHandler::SPostProcessingPair EffectHandler::obtainScreenQuadMaterialFromFile(const irr::core::stringc& filename,
        irr::video::E_MATERIAL_TYPE baseMaterial)
{
    CShaderPreprocessor sPP(driver);

    sPP.addShaderDefine("SCREENX", core::stringc(ScreenRTTSize.Width));
    sPP.addShaderDefine("SCREENY", core::stringc(ScreenRTTSize.Height));

    video::E_VERTEX_SHADER_TYPE VertexLevel = driver->queryFeature(video::EVDF_VERTEX_SHADER_3_0) ? EVST_VS_3_0 : EVST_VS_2_0;
    video::E_PIXEL_SHADER_TYPE PixelLevel = driver->queryFeature(video::EVDF_PIXEL_SHADER_3_0) ? EPST_PS_3_0 : EPST_PS_2_0;

    E_SHADER_EXTENSION shaderExt = (driver->getDriverType() == EDT_DIRECT3D9) ? ESE_HLSL : ESE_GLSL;

    video::IGPUProgrammingServices* gpu = driver->getGPUProgrammingServices();

    const stringc shaderString = sPP.ppShaderFF(filename.c_str());

    ScreenQuadCB* SQCB = new ScreenQuadCB(this, true);

    s32 PostMat = gpu->addHighLevelShaderMaterial(
                      sPP.ppShader(SCREEN_QUAD_V[shaderExt]).c_str(), "vertexMain", VertexLevel,
                      shaderString.c_str(), "pixelMain", PixelLevel,
                      SQCB, baseMaterial);

    SPostProcessingPair pPair(PostMat, SQCB);

    SQCB->drop();

    return pPair;
}


void EffectHandler::setPostProcessingEffectConstant(const irr::s32 materialType, const irr::core::stringc& name,
        const f32* data, const irr::u32 count)
{
    SPostProcessingPair tempPair(materialType, 0);
    s32 matIndex = PostProcessingRoutines.binary_search(tempPair);

    if (matIndex != -1)
        PostProcessingRoutines[matIndex].callback->uniformDescriptors[name] = ScreenQuadCB::SUniformDescriptor(data, count);
}


s32 EffectHandler::addPostProcessingEffectFromFile(const irr::core::stringc& filename,
        IPostProcessingRenderCallback* callback)
{
    SPostProcessingPair pPair = obtainScreenQuadMaterialFromFile(filename);
    pPair.renderCallback = callback;
    PostProcessingRoutines.push_back(pPair);

    return pPair.materialType;
}

// Copyright (C) 2007-2009 Ahmed Hilali
