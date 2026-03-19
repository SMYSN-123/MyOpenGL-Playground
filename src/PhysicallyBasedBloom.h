#ifndef PHYSICALLYBASEDBLOOM_H
#define PHYSICALLYBASEDBLOOM_H

#include <iostream>
#include <vector>
#include "Shader.h"
#include "PostProcessor.h"
#include <glad/glad.h>

#include <glm.hpp>
#include <gtc/matrix_transform.hpp>
#include <gtc/type_ptr.hpp>

unsigned int PostProcessor::emptyVAO = 0;

struct bloomMip
{
    glm::vec2 size;
    glm::ivec2 intSize;
    unsigned int texture;
};

class bloomFBO
{
public:
    bloomFBO() : mInit(false)
    {

    }

    ~bloomFBO()
    {

    }

    bool Init(unsigned int windowWidth, unsigned int windowHeight, unsigned int mipChainLength)
    {
        if(mInit)
        {
            return true;
        }

        glGenFramebuffers(1, &mFBO);
        glBindFramebuffer(GL_FRAMEBUFFER, mFBO);

        glm::vec2 mipSize(static_cast<float>(windowWidth), static_cast<float>(windowHeight));
        glm::ivec2 mipIntSize(static_cast<int>(windowWidth), static_cast<int>(windowHeight));

        // Safety check
        if (windowWidth > static_cast<float>(INT_MAX) || windowHeight > static_cast<float>(INT_MAX))
        {
            std::cerr << "Window size conversion overflow - cannot build bloom FBO!\n";
            return false;
        }

        for (unsigned int i = 0; i < mipChainLength; i++)
        {
            bloomMip mip;

            mipSize *= 0.5;
            mipIntSize /= 2;
            mip.size = mipSize;
            mip.intSize = mipIntSize;

            glGenTextures(1, &mip.texture);
            glBindTexture(GL_TEXTURE_2D, mip.texture);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_R11F_G11F_B10F, mipIntSize.x, mipIntSize.y, 0, GL_RGB, GL_FLOAT, nullptr);

            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

            mMipChain.emplace_back(mip);
        }

        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, mMipChain[0].texture, 0);

        unsigned int attachments[1] = { GL_COLOR_ATTACHMENT0 };
        glDrawBuffers(1, attachments);

        // check completion status
        int status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        if (status != GL_FRAMEBUFFER_COMPLETE)
        {
            printf("gbuffer FBO error, status: 0x%x\n", status);
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            return false;
        }

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        mInit = true;
        return true;
    }

    void Destroy()
    {
        for (auto& i : mMipChain)
        {
            glDeleteTextures(1, &i.texture);
            i.texture = 0;
        }
        glDeleteFramebuffers(1, &mFBO);
        mFBO = 0;
        mInit = false;
    }

    void BindForWriting()
    {
        glBindFramebuffer(GL_FRAMEBUFFER, mFBO);
    }

    const std::vector<bloomMip>& MipChain() const
    {
        return mMipChain;
    }
private:
    bool mInit;
    unsigned int mFBO;
    std::vector<bloomMip> mMipChain;
};

class BloomRenderer
{
public:
    BloomRenderer() : mInit(false)
    {

    }

    ~BloomRenderer()
    {

    }

    bool Init(unsigned int windowWidth, unsigned int windowHeight)
    {
        if (mInit)
        {
            return true;
        }

        mSrcViewportSize = glm::ivec2(static_cast<int>(windowWidth), static_cast<int>(windowHeight));
        mSrcViewportSizeFloat = glm::vec2(static_cast<float>(windowWidth), static_cast<float>(windowHeight));

        const unsigned int num_bloom_mips = 5;
        bool status = mFBO.Init(windowWidth, windowHeight, num_bloom_mips);
        if (!status)
        {
            std::cerr << "Failed to initialize bloom FBO - cannot create bloom renderer!\n";
            return false;
        }

        mDownsampleShader = Shader("../src/Downsampling.vs", "../src/Downsampling.fs");
        mUpsampleShader = Shader("../src/Upsampling.vs", "../src/Upsampling.fs");

        mDownsampleShader.use();
        mDownsampleShader.setInt("srcTexture", 0);

        mUpsampleShader.use();
        mUpsampleShader.setInt("srcTexture", 0);

        mInit = true;

        return true;
    }

    void Destroy()
    {
        mFBO.Destroy();
        mDownsampleShader.destroy();
        mUpsampleShader.destroy();
        mInit = false;
    }

    void RenderBloomTexture(unsigned int srcTexture, float filterRadius)
    {
        mFBO.BindForWriting();

        this->RenderDownsamples(srcTexture);
        this->RenderUpsamples(filterRadius);

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, mSrcViewportSize.x, mSrcViewportSize.y);
    }

    unsigned int BloomTexture()
    {
        return mFBO.MipChain()[0].texture;
    }
private:
    void RenderDownsamples(unsigned int srcTexture)
    {
        const std::vector<bloomMip>& mipChain = mFBO.MipChain();

        mDownsampleShader.use();
        mDownsampleShader.setVec2("srcResolution", mSrcViewportSizeFloat);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, srcTexture);

        for (int i = 0; i < mipChain.size(); i++)
        {
            const bloomMip& mip = mipChain[i];
            glViewport(0, 0, mip.intSize.x, mip.intSize.y);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, mip.texture, 0);

            PostProcessor::RenderFullScreen();

            mDownsampleShader.setVec2("srcResolution", mip.size);
            glBindTexture(GL_TEXTURE_2D, mip.texture);
        }
    }

    void RenderUpsamples(float filterRadius)
    {
        const std::vector<bloomMip>& mipChain = mFBO.MipChain();

        mUpsampleShader.use();
        mUpsampleShader.setFloat("filterRadius", filterRadius);

        glEnable(GL_BLEND);
        glBlendFunc(GL_ONE, GL_ONE);
        glBlendEquation(GL_FUNC_ADD);

        for (int i = static_cast<int>(mipChain.size()) - 1; i > 0; i--)
        {
            const bloomMip& mip = mipChain[i];
            const bloomMip& nextMip = mipChain[i - 1];

            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, mip.texture);

            glViewport(0, 0, nextMip.intSize.x, nextMip.intSize.y);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, nextMip.texture, 0);

            PostProcessor::RenderFullScreen();
        }
        glDisable(GL_BLEND);
    }

    bool mInit;
    bloomFBO mFBO;
    glm::ivec2 mSrcViewportSize;
    glm::vec2 mSrcViewportSizeFloat;
    Shader mDownsampleShader;
    Shader mUpsampleShader;
};

#endif