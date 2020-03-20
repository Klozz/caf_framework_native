/*
 * Copyright 2019 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define ATRACE_TAG ATRACE_TAG_GRAPHICS

#include "BlurFilter.h"
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <GLES3/gl3ext.h>
#include <ui/GraphicTypes.h>
#include <cstdint>

#include <utils/Trace.h>

namespace android {
namespace renderengine {
namespace gl {

BlurFilter::BlurFilter(GLESRenderEngine& engine)
      : mEngine(engine),
        mCompositionFbo(engine),
        mPingFbo(engine),
        mPongFbo(engine),
        mMixProgram(engine),
        mBlurProgram(engine) {
    mMixProgram.compile(getVertexShader(), getMixFragShader());
    mMPosLoc = mMixProgram.getAttributeLocation("aPosition");
    mMUvLoc = mMixProgram.getAttributeLocation("aUV");
    mMTextureLoc = mMixProgram.getUniformLocation("uTexture");
    mMCompositionTextureLoc = mMixProgram.getUniformLocation("uCompositionTexture");
    mMMixLoc = mMixProgram.getUniformLocation("uMix");

    mBlurProgram.compile(getVertexShader(), getFragmentShader());
    mBPosLoc = mBlurProgram.getAttributeLocation("aPosition");
    mBUvLoc = mBlurProgram.getAttributeLocation("aUV");
    mBTextureLoc = mBlurProgram.getUniformLocation("uTexture");
    mBOffsetLoc = mBlurProgram.getUniformLocation("uOffset");
}

status_t BlurFilter::setAsDrawTarget(const DisplaySettings& display, uint32_t radius) {
    ATRACE_NAME("BlurFilter::setAsDrawTarget");
    mRadius = radius;

    if (!mTexturesAllocated) {
        mDisplayWidth = display.physicalDisplay.width();
        mDisplayHeight = display.physicalDisplay.height();
        mCompositionFbo.allocateBuffers(mDisplayWidth, mDisplayHeight);

        const uint32_t fboWidth = floorf(mDisplayWidth * kFboScale);
        const uint32_t fboHeight = floorf(mDisplayHeight * kFboScale);
        mPingFbo.allocateBuffers(fboWidth, fboHeight);
        mPongFbo.allocateBuffers(fboWidth, fboHeight);
        mTexturesAllocated = true;
    }

    if (mPingFbo.getStatus() != GL_FRAMEBUFFER_COMPLETE) {
        ALOGE("Invalid blur buffer");
        return mPingFbo.getStatus();
    }
    if (mCompositionFbo.getStatus() != GL_FRAMEBUFFER_COMPLETE) {
        ALOGE("Invalid composition buffer");
        return mCompositionFbo.getStatus();
    }

    mCompositionFbo.bind();
    glViewport(0, 0, mCompositionFbo.getBufferWidth(), mCompositionFbo.getBufferHeight());
    return NO_ERROR;
}

void BlurFilter::drawMesh(GLuint uv, GLuint position) {
    static constexpr auto size = 2.0f;
    static constexpr auto translation = 1.0f;
    GLfloat positions[] = {
        translation-size, -translation-size,
        translation-size, -translation+size,
        translation+size, -translation+size
    };
    GLfloat texCoords[] = {
        0.0f, 0.0f-translation,
        0.0f, size-translation,
        size, size-translation
    };

    // set attributes
    glEnableVertexAttribArray(uv);
    glVertexAttribPointer(uv, 2 /* size */, GL_FLOAT, GL_FALSE, 0, texCoords);
    glEnableVertexAttribArray(position);
    glVertexAttribPointer(position, 2 /* size */, GL_FLOAT, GL_FALSE, 2 * sizeof(GLfloat),
                          positions);

    // draw mesh
    glDrawArrays(GL_TRIANGLES, 0 /* first */, 3 /* count */);
    mEngine.checkErrors("Drawing blur mesh");
}

status_t BlurFilter::prepare() {
    ATRACE_NAME("BlurFilter::prepare");

    if (mPongFbo.getStatus() != GL_FRAMEBUFFER_COMPLETE) {
        ALOGE("Invalid FBO");
        return mPongFbo.getStatus();
    }
    if (!mBlurProgram.isValid()) {
        ALOGE("Invalid shader");
        return GL_INVALID_OPERATION;
    }

    blit(mCompositionFbo, mPingFbo);

    // Kawase is an approximation of Gaussian, but it behaves differently from it.
    // A radius transformation is required for approximating them, and also to introduce
    // non-integer steps, necessary to smoothly interpolate large radii.
    auto radius = mRadius / 6.0f;

    // Calculate how many passes we'll do, based on the radius.
    // Too many passes will make the operation expensive.
    auto passes = min(kMaxPasses, (uint32_t)ceil(radius));

    // We'll ping pong between our textures, to accumulate the result of various offsets.
    mBlurProgram.useProgram();
    GLFramebuffer* read = &mPingFbo;
    GLFramebuffer* draw = &mPongFbo;
    float stepX = radius / (float)mCompositionFbo.getBufferWidth() / (float)passes;
    float stepY = radius / (float)mCompositionFbo.getBufferHeight() / (float)passes;
    glActiveTexture(GL_TEXTURE0);
    glUniform1i(mBTextureLoc, 0);
    for (auto i = 0; i < passes; i++) {
        ATRACE_NAME("BlurFilter::renderPass");
        draw->bind();

        glViewport(0, 0, draw->getBufferWidth(), draw->getBufferHeight());
        glBindTexture(GL_TEXTURE_2D, read->getTextureName());
        glUniform2f(mBOffsetLoc, stepX * i, stepY * i);
        mEngine.checkErrors("Setting uniforms");

        drawMesh(mBUvLoc, mBPosLoc);

        // Swap buffers for next iteration
        auto tmp = draw;
        draw = read;
        read = tmp;
    }
    mLastDrawTarget = read;

    // Cleanup
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    return NO_ERROR;
}

status_t BlurFilter::render(bool multiPass) {
    ATRACE_NAME("BlurFilter::render");

    // Now let's scale our blur up. It will be interpolated with the larger composited
    // texture for the first frames, to hide downscaling artifacts.
    GLfloat mix = fmin(1.0, mRadius / kMaxCrossFadeRadius);

    // When doing multiple passes, we cannot try to read mCompositionFbo, given that we'll
    // be writing onto it. Let's disable the crossfade, otherwise we'd need 1 extra frame buffer,
    // as large as the screen size.
    if (mix >= 1 || multiPass) {
        mLastDrawTarget->bindAsReadBuffer();
        glBlitFramebuffer(0, 0, mLastDrawTarget->getBufferWidth(),
                          mLastDrawTarget->getBufferHeight(), 0, 0, mDisplayWidth, mDisplayHeight,
                          GL_COLOR_BUFFER_BIT, GL_LINEAR);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
        return NO_ERROR;
    }

    mMixProgram.useProgram();
    glUniform1f(mMMixLoc, mix);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, mLastDrawTarget->getTextureName());
    glUniform1i(mMTextureLoc, 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, mCompositionFbo.getTextureName());
    glUniform1i(mMCompositionTextureLoc, 1);
    mEngine.checkErrors("Setting final pass uniforms");

    drawMesh(mMUvLoc, mMPosLoc);

    glUseProgram(0);
    glActiveTexture(GL_TEXTURE0);
    return NO_ERROR;
}

string BlurFilter::getVertexShader() const {
    return R"SHADER(#version 310 es

        in vec2 aPosition;
        in highp vec2 aUV;
        out highp vec2 vUV;

        void main() {
            vUV = aUV;
            gl_Position = vec4(aPosition, 0.0, 1.0);
        }
    )SHADER";
}

string BlurFilter::getFragmentShader() const {
    return R"SHADER(#version 310 es
        precision mediump float;

        uniform sampler2D uTexture;
        uniform vec2 uOffset;

        highp in vec2 vUV;
        out vec4 fragColor;

        void main() {
            fragColor  = texture(uTexture, vUV, 0.0);
            fragColor += texture(uTexture, vUV + vec2( uOffset.x,  uOffset.y), 0.0);
            fragColor += texture(uTexture, vUV + vec2( uOffset.x, -uOffset.y), 0.0);
            fragColor += texture(uTexture, vUV + vec2(-uOffset.x,  uOffset.y), 0.0);
            fragColor += texture(uTexture, vUV + vec2(-uOffset.x, -uOffset.y), 0.0);

            fragColor = vec4(fragColor.rgb * 0.2, 1.0);
        }
    )SHADER";
}

string BlurFilter::getMixFragShader() const {
    string shader = R"SHADER(#version 310 es
        precision mediump float;

        in highp vec2 vUV;
        out vec4 fragColor;

        uniform sampler2D uCompositionTexture;
        uniform sampler2D uTexture;
        uniform float uMix;

        void main() {
            vec4 blurred = texture(uTexture, vUV);
            vec4 composition = texture(uCompositionTexture, vUV);
            fragColor = mix(composition, blurred, uMix);
        }
    )SHADER";
    return shader;
}

void BlurFilter::blit(GLFramebuffer& read, GLFramebuffer& draw) const {
    read.bindAsReadBuffer();
    draw.bindAsDrawBuffer();
    glBlitFramebuffer(0, 0, read.getBufferWidth(), read.getBufferHeight(), 0, 0,
                      draw.getBufferWidth(), draw.getBufferHeight(), GL_COLOR_BUFFER_BIT,
                      GL_LINEAR);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

} // namespace gl
} // namespace renderengine
} // namespace android
