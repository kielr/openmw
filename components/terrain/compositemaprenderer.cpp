#include "compositemaprenderer.hpp"

#include <OpenThreads/ScopedLock>

#include <osg/FrameBufferObject>
#include <osg/Texture2D>
#include <osg/RenderInfo>

namespace Terrain
{

CompositeMapRenderer::CompositeMapRenderer()
    : mTimeAvailable(0.0005)
{
    setSupportsDisplayList(false);
    setCullingActive(false);

    mFBO = new osg::FrameBufferObject;

    getOrCreateStateSet()->setMode(GL_LIGHTING, osg::StateAttribute::OFF);
}

void CompositeMapRenderer::drawImplementation(osg::RenderInfo &renderInfo) const
{
    mCompiled.clear();

    OpenThreads::ScopedLock<OpenThreads::Mutex> lock(mMutex);

    if (mImmediateCompileSet.empty() && mCompileSet.empty())
        return;

    while (!mImmediateCompileSet.empty())
    {
        CompositeMap* node = *mImmediateCompileSet.begin();
        mCompiled.insert(node);

        compile(*node, renderInfo, NULL);

        mImmediateCompileSet.erase(mImmediateCompileSet.begin());
    }

    double timeLeft = mTimeAvailable;

    while (!mCompileSet.empty() && timeLeft > 0)
    {
        CompositeMap* node = *mCompileSet.begin();

        compile(*node, renderInfo, &timeLeft);

        if (node->mCompiled >= node->mDrawables.size())
        {
            mCompiled.insert(node);
            mCompileSet.erase(mCompileSet.begin());
        }
    }
}

void CompositeMapRenderer::compile(CompositeMap &compositeMap, osg::RenderInfo &renderInfo, double* timeLeft) const
{
    // if there are no more external references we can assume the texture is no longer required
    if (compositeMap.mTexture->referenceCount() <= 1)
    {
        compositeMap.mCompiled = compositeMap.mDrawables.size();
        return;
    }

    osg::Timer timer;
    osg::State& state = *renderInfo.getState();
    osg::GLExtensions* ext = state.get<osg::GLExtensions>();

    if (!mFBO)
        return;

    if (!ext->isFrameBufferObjectSupported)
        return;

    osg::FrameBufferAttachment attach (compositeMap.mTexture);
    mFBO->setAttachment(osg::Camera::COLOR_BUFFER, attach);
    mFBO->apply(state, osg::FrameBufferObject::DRAW_FRAMEBUFFER);

    GLenum status = ext->glCheckFramebufferStatus(GL_FRAMEBUFFER_EXT);

    if (status != GL_FRAMEBUFFER_COMPLETE_EXT)
    {
        GLuint fboId = state.getGraphicsContext() ? state.getGraphicsContext()->getDefaultFboId() : 0;
        ext->glBindFramebuffer(GL_FRAMEBUFFER_EXT, fboId);
        OSG_ALWAYS << "Error attaching FBO" << std::endl;
        return;
    }

    for (unsigned int i=compositeMap.mCompiled; i<compositeMap.mDrawables.size(); ++i)
    {
        osg::Drawable* drw = compositeMap.mDrawables[i];
        osg::StateSet* stateset = drw->getStateSet();

        if (stateset)
            renderInfo.getState()->pushStateSet(stateset);

        renderInfo.getState()->apply();

        glViewport(0,0,compositeMap.mTexture->getTextureWidth(), compositeMap.mTexture->getTextureHeight());
        drw->drawImplementation(renderInfo);

        if (stateset)
            renderInfo.getState()->popStateSet();

        ++compositeMap.mCompiled;

        if (timeLeft)
        {
            *timeLeft -= timer.time_s();
            timer.setStartTick();

            if (*timeLeft <= 0)
                break;
        }
    }

    state.haveAppliedAttribute(osg::StateAttribute::VIEWPORT);

    GLuint fboId = state.getGraphicsContext() ? state.getGraphicsContext()->getDefaultFboId() : 0;
    ext->glBindFramebuffer(GL_FRAMEBUFFER_EXT, fboId);
}

void CompositeMapRenderer::setTimeAvailableForCompile(double time)
{
    mTimeAvailable = time;
}

void CompositeMapRenderer::addCompositeMap(CompositeMap* compositeMap, bool immediate)
{
    OpenThreads::ScopedLock<OpenThreads::Mutex> lock(mMutex);
    if (immediate)
        mImmediateCompileSet.insert(compositeMap);
    else
        mCompileSet.insert(compositeMap);
}

void CompositeMapRenderer::setImmediate(CompositeMap* compositeMap)
{
    OpenThreads::ScopedLock<OpenThreads::Mutex> lock(mMutex);
    CompileSet::iterator found = mCompileSet.find(compositeMap);
    if (found == mCompileSet.end())
        return;
    else
    {
        mImmediateCompileSet.insert(compositeMap);
        mCompileSet.erase(found);
    }
}

unsigned int CompositeMapRenderer::getCompileSetSize() const
{
    OpenThreads::ScopedLock<OpenThreads::Mutex> lock(mMutex);
    return mCompileSet.size();
}

CompositeMap::CompositeMap()
    : mCompiled(0)
{
}

CompositeMap::~CompositeMap()
{

}



}
