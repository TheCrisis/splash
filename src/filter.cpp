#include "filter.h"

#include "cgUtils.h"
#include "log.h"
#include "scene.h"
#include "texture_image.h"
#include "timer.h"

using namespace std;

namespace Splash
{

/*************/
Filter::Filter(std::weak_ptr<RootObject> root)
    : Texture(root)
{
    init();
}

/*************/
void Filter::init()
{
    _type = "filter";
    _renderingPriority = Priority::FILTER;
    registerAttributes();

    // If the root object weak_ptr is expired, this means that
    // this object has been created outside of a World or Scene.
    // This is used for getting documentation "offline"
    if (_root.expired())
        return;

    // Intialize FBO, textures and everything OpenGL
    glGetError();
    glGenFramebuffers(1, &_fbo);

    setOutput();

    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, _fbo);
    GLenum _status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (_status != GL_FRAMEBUFFER_COMPLETE)
    {
        Log::get() << Log::WARNING << "Filter::" << __FUNCTION__ << " - Error while initializing framebuffer object: " << _status << Log::endl;
        return;
    }
    else
        Log::get() << Log::MESSAGE << "Filter::" << __FUNCTION__ << " - Framebuffer object successfully initialized" << Log::endl;

    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);

    GLenum error = glGetError();
    if (error)
    {
        Log::get() << Log::WARNING << "Filter::" << __FUNCTION__ << " - Error while binding framebuffer" << Log::endl;
        _isInitialized = false;
    }
    else
    {
        Log::get() << Log::MESSAGE << "Filter::" << __FUNCTION__ << " - Filter correctly initialized" << Log::endl;
        _isInitialized = true;
    }
}

/*************/
Filter::~Filter()
{
    if (_root.expired())
        return;

#ifdef DEBUG
    Log::get() << Log::DEBUGGING << "Filter::~Filter - Destructor" << Log::endl;
#endif

    glDeleteFramebuffers(1, &_fbo);
}

/*************/
void Filter::bind()
{
    _outTexture->bind();
}

/*************/
unordered_map<string, Values> Filter::getShaderUniforms() const
{
    unordered_map<string, Values> uniforms;
    return uniforms;
}

/*************/
bool Filter::linkTo(std::shared_ptr<BaseObject> obj)
{
    // Mandatory before trying to link
    if (!obj || !Texture::linkTo(obj))
        return false;

    if (dynamic_pointer_cast<Texture>(obj).get() != nullptr)
    {
        if (!_inTexture.expired())
            _screen->removeTexture(_inTexture.lock());

        auto tex = dynamic_pointer_cast<Texture>(obj);
        _screen->addTexture(tex);
        _inTexture = tex;

        return true;
    }
    else if (dynamic_pointer_cast<Image>(obj).get() != nullptr)
    {
        auto tex = make_shared<Texture_Image>(_root);
        tex->setName(getName() + "_" + obj->getName() + "_tex");
        if (tex->linkTo(obj))
        {
            _root.lock()->registerObject(tex);
            return linkTo(tex);
        }
        else
        {
            return false;
        }
    }

    return true;
}

/*************/
void Filter::unbind()
{
    _outTexture->unbind();
}

/*************/
void Filter::unlinkFrom(std::shared_ptr<BaseObject> obj)
{
    if (dynamic_pointer_cast<Texture>(obj).get() != nullptr)
    {
        if (_inTexture.expired())
            return;

        auto inTex = _inTexture.lock();
        auto tex = dynamic_pointer_cast<Texture>(obj);

        _screen->removeTexture(tex);
        if (tex->getName() == inTex->getName())
            _inTexture.reset();
    }
    else if (dynamic_pointer_cast<Image>(obj).get() != nullptr)
    {
        auto textureName = getName() + "_" + obj->getName() + "_tex";
        auto tex = _root.lock()->unregisterObject(textureName);

        if (tex)
        {
            tex->unlinkFrom(obj);
            unlinkFrom(tex);
        }
    }

    Texture::unlinkFrom(obj);
}

/*************/
void Filter::render()
{
    if (_inTexture.expired())
        return;

    // Execute waiting tasks
    {
        lock_guard<mutex> lockTask(_taskMutex);
        for (auto& task : _taskQueue)
            task();
        _taskQueue.clear();
    }

    if (_updateColorDepth)
        updateColorDepth();

    auto input = _inTexture.lock();
    _outTextureSpec = input->getSpec();
    _outTexture->resize(_outTextureSpec.width, _outTextureSpec.height);
    glViewport(0, 0, _outTextureSpec.width, _outTextureSpec.height);

    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, _fbo);
    GLenum fboBuffers[1] = {GL_COLOR_ATTACHMENT0};
    glDrawBuffers(1, fboBuffers);
    glDisable(GL_DEPTH_TEST);

    glClearColor(0.0, 0.0, 0.0, 0.0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    _screen->activate();
    updateUniforms();
    _screen->draw();
    _screen->deactivate();

    glDisable(GL_DEPTH_TEST);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);

    _outTexture->generateMipmap();
}

/*************/
void Filter::updateUniforms()
{
    auto shader = _screen->getShader();

    // Update generic uniforms
    for (auto& weakObject : _linkedObjects)
    {
        auto scene = dynamic_pointer_cast<Scene>(_root.lock());

        auto obj = weakObject.lock();
        if (obj)
        {
            if (obj->getType() == "image")
            {
                Values remainingTime, duration;
                obj->getAttribute("duration", duration);
                obj->getAttribute("remaining", remainingTime);
                if (remainingTime.size() == 1)
                    shader->setAttribute("uniform", {"_filmRemaining", remainingTime[0].as<float>()});
                if (duration.size() == 1)
                    shader->setAttribute("uniform", {"_filmDuration", duration[0].as<float>()});
            }
        }
    }

    // Update uniforms specific to the current filtering shader
    for (auto& uniform : _filterUniforms)
    {
        Values param;
        param.push_back(uniform.first);
        for (auto& v : uniform.second)
            param.push_back(v);
        shader->setAttribute("uniform", param);
    }
}

/*************/
void Filter::setOutput()
{
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, _fbo);

    _outTexture = make_shared<Texture_Image>(_root);
    _outTexture->setAttribute("filtering", {1});
    _outTexture->reset(GL_TEXTURE_2D, 0, GL_RGBA, 512, 512, 0, GL_RGBA, GL_UNSIGNED_INT_8_8_8_8_REV, nullptr);
    glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, _outTexture->getTexId(), 0);

    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);

    // Setup the virtual screen
    _screen = make_shared<Object>(_root);
    _screen->setAttribute("fill", {"filter"});
    auto virtualScreen = make_shared<Geometry>(_root);
    _screen->addGeometry(virtualScreen);
}

/*************/
void Filter::updateColorDepth()
{
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, _fbo);
    glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0);
    auto spec = _outTexture->getSpec();
    if (_render16bits)
        _outTexture->reset(GL_TEXTURE_2D, 0, GL_RGBA16, spec.width, spec.height, 0, GL_RGBA, GL_UNSIGNED_SHORT, nullptr);
    else
        _outTexture->reset(GL_TEXTURE_2D, 0, GL_RGBA, spec.width, spec.height, 0, GL_RGBA, GL_UNSIGNED_INT_8_8_8_8_REV, nullptr);
    glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, _outTexture->getTexId(), 0);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    _updateColorDepth = false;
}

/*************/
bool Filter::setFilterSource(const string& source)
{
    _screen->setAttribute("fill", {"userDefined"});

    auto shader = _screen->getShader();
    map<Shader::ShaderType, string> shaderSources;
    shaderSources[Shader::ShaderType::fragment] = source;
    if (!shader->setSource(shaderSources))
    {
        Log::get() << Log::WARNING << "Filter::" << __FUNCTION__ << " - Could not apply shader filter" << Log::endl;
        return false;
    }

    // This is a trick to force the shader compilation
    _screen->activate();
    _screen->deactivate();

    // Unregister previous automatically added uniforms
    _attribFunctions.clear();
    registerAttributes();

    // Register the attributes corresponding to the shader uniforms
    auto uniforms = shader->getUniforms();
    for (auto& u : uniforms)
    {
        vector<char> types;
        for (auto& v : u.second)
            types.push_back(v.getTypeAsChar());

        _filterUniforms[u.first] = u.second;
        addAttribute(u.first,
            [=](const Values& args) {
                _filterUniforms[u.first] = args;
                return true;
            },
            [=]() -> Values { return _filterUniforms[u.first]; },
            types);
    }

    return true;
}

/*************/
void Filter::registerAttributes()
{
    addAttribute("16bits",
        [&](const Values& args) {
            bool render16bits = args[0].as<int>();
            if (render16bits != _render16bits)
            {
                _render16bits = render16bits;
                _updateColorDepth = true;
            }
            return true;
        },
        [&]() -> Values { return {(int)_render16bits}; },
        {'n'});
    setAttributeDescription("16bits", "Set to 1 for the filter to be rendered in 16bits per component (otherwise 8bpc)");

    addAttribute("blackLevel",
        [&](const Values& args) {
            auto blackLevel = args[0].as<float>();
            blackLevel = std::max(0.f, std::min(1.f, blackLevel));
            _filterUniforms["_blackLevel"] = {blackLevel};
            return true;
        },
        [&]() -> Values {
            auto it = _filterUniforms.find("_blackLevel");
            if (it == _filterUniforms.end())
                _filterUniforms["_blackLevel"] = {0.f}; // Default value
            return _filterUniforms["_blackLevel"];
        },
        {'n'});
    setAttributeDescription("blackLevel", "Set the black level for the linked texture");

    addAttribute("brightness",
        [&](const Values& args) {
            auto brightness = args[0].as<float>();
            brightness = std::max(0.f, std::min(2.f, brightness));
            _filterUniforms["_brightness"] = {brightness};
            return true;
        },
        [&]() -> Values {
            auto it = _filterUniforms.find("_brightness");
            if (it == _filterUniforms.end())
                _filterUniforms["_brightness"] = {1.f}; // Default value
            return _filterUniforms["_brightness"];
        },
        {'n'});
    setAttributeDescription("brightness", "Set the brightness for the linked texture");

    addAttribute("contrast",
        [&](const Values& args) {
            auto contrast = args[0].as<float>();
            contrast = std::max(0.f, std::min(2.f, contrast));
            _filterUniforms["_contrast"] = {contrast};
            return true;
        },
        [&]() -> Values {
            auto it = _filterUniforms.find("_contrast");
            if (it == _filterUniforms.end())
                _filterUniforms["_contrast"] = {1.f}; // Default value
            return _filterUniforms["contrast"];
        },
        {'n'});
    setAttributeDescription("contrast", "Set the contrast for the linked texture");

    addAttribute("colorTemperature",
        [&](const Values& args) {
            auto colorTemperature = args[0].as<float>();
            colorTemperature = std::max(0.f, std::min(16000.f, colorTemperature));
            _filterUniforms["_colorTemperature"] = {colorTemperature};
            auto colorBalance = colorBalanceFromTemperature(colorTemperature);
            _filterUniforms["_colorBalance"] = {colorBalance.x, colorBalance.y};
            return true;
        },
        [&]() -> Values {
            auto it = _filterUniforms.find("_colorTemperature");
            if (it == _filterUniforms.end())
                _filterUniforms["_colorTemperature"] = {6500.f}; // Default value
            return _filterUniforms["_colorTemperature"];
        },
        {'n'});
    setAttributeDescription("colorTemperature", "Set the color temperature correction for the linked texture");

    addAttribute("invertChannels",
        [&](const Values& args) {
            auto enable = args[0].as<int>();
            enable = std::min(1, std::max(0, enable));
            _filterUniforms["_invertChannels"] = {enable};
            return true;
        },
        [&]() -> Values {
            auto it = _filterUniforms.find("_invertChannels");
            if (it == _filterUniforms.end())
                _filterUniforms["_invertChannels"] = {0};
            return _filterUniforms["_invertChannels"];
        },
        {'n'});
    setAttributeDescription("invertChannels", "Invert red and blue channels");

    addAttribute("saturation",
        [&](const Values& args) {
            auto saturation = args[0].as<float>();
            saturation = std::max(0.f, std::min(2.f, saturation));
            _filterUniforms["_saturation"] = {saturation};
            return true;
        },
        [&]() -> Values {
            auto it = _filterUniforms.find("_saturation");
            if (it == _filterUniforms.end())
                _filterUniforms["_saturation"] = {1.f}; // Default value
            return _filterUniforms["_saturation"];
        },
        {'n'});
    setAttributeDescription("saturation", "Set the saturation for the linked texture");

    addAttribute("filterSource",
        [&](const Values& args) {
            auto src = args[0].as<string>();
            if (src.size() == 0)
                return true; // No shader specified
            _shaderSource = src;
            addTask([=]() { setFilterSource(src); });
            return true;
        },
        [&]() -> Values { return {_shaderSource}; },
        {'s'});
    setAttributeDescription("setFilterSource", "Set the fragment shader source for the filter");
}

} // end of namespace
