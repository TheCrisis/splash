/*
 * Copyright (C) 2013 Emmanuel Durand
 *
 * This file is part of Splash.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * blobserver is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with blobserver.  If not, see <http://www.gnu.org/licenses/>.
 */

/*
 * @window.h
 * The Window class
 */

#ifndef SPLASH_WINDOW_H
#define SPLASH_WINDOW_H

#include "config.h"
#include "coretypes.h"
#include "basetypes.h"

#include <atomic>
#include <deque>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

#include <glm/glm.hpp>

namespace Splash {

class Window : public BaseObject
{
    public:
        /**
         * Constructor
         */
        Window(RootObjectWeakPtr root);

        /**
         * Destructor
         */
        ~Window();

        /**
         * No copy constructor, but a move one
         */
        Window(const Window&) = delete;
        Window& operator=(const Window&) = delete;

        Window(Window&& w)
        {
            *this = std::move(w);
        }

        Window& operator=(Window&& w)
        {
            if (this != &w)
            {
                _isInitialized = w._isInitialized;
                _window = w._window;
                _screenId = w._screenId;
                _fullscreen = std::move(w._fullscreen);
                _layout = w._layout;
                _swapInterval = w._swapInterval;

                _screen = w._screen;
                _viewProjectionMatrix = w._viewProjectionMatrix;
                _inTextures = w._inTextures;

                _renderFbo = w._renderFbo;
                _readFbo = w._readFbo;
                _renderFence = w._renderFence;
                _depthTexture = w._depthTexture;
                _colorTexture = w._colorTexture;
            }
            return *this;
        }

        /**
         * Get grabbed character (not necesseraly a specific key)
         */
        static int getChars(GLFWwindow*& win, unsigned int& codepoint);

        /**
         * Get the grabbed key
         */
        bool getKey(int key);
        static int getKeys(GLFWwindow*& win, int& key, int& action, int& mods);

        /**
         * Get the grabbed mouse action
         */
        static int getMouseBtn(GLFWwindow*& win, int& btn, int& action, int& mods);

        /**
         * Get the mouse position
         */
        static void getMousePos(GLFWwindow*& win, int& xpos, int& ypos);

        /**
         * Get the mouse position
         */
        static int getScroll(GLFWwindow*& win, double& xoffset, double& yoffset);

        /**
         * Get the quit flag status
         */
        static int getQuitFlag() {return _quitFlag;}

        /**
         * Check wether it is initialized
         */
        bool isInitialized() const {return _isInitialized;}

        /**
         * Check wether the given GLFW window is related to this object
         */
        bool isWindow(GLFWwindow* w) const {return (w == _window->get() ? true : false);}

        /**
         * Try to link / unlink the given BaseObject to this
         */
        bool linkTo(BaseObjectPtr obj);
        bool unlinkFrom(BaseObjectPtr obj);

        /**
         * Render this window to screen
         */
        bool render();

        /**
         * Set the window to fullscreen
         */
        bool switchFullscreen(int screenId = -1);

        /**
         * Set / unset a new texture to draw
         */
        void setTexture(TexturePtr tex);
        void unsetTexture(TexturePtr tex);

        /**
         * Swap the back and front buffers
         */
        static void swapLoopNotify();
        void swapBuffers();

    private:
        bool _isInitialized {false};
        GlWindowPtr _window;
        int _screenId {-1};
        bool _fullscreen {false};
        bool _withDecoration {true};
        int _windowRect[4];
        bool _srgb {true};
        float _gammaCorrection {2.2f};
        Values _layout {0, 0, 0, 0};
        int _swapInterval {2};

        // Swapping thread
        std::thread _swapThread;
        static std::mutex _swapLoopMutex;
        static std::mutex _swapLoopNotifyMutex;
        std::atomic_bool _swapLoopContinue {false};
        static std::condition_variable _swapCondition;
        static std::condition_variable _swapConditionNotify;

        // Offscreen rendering related objects
        GLuint _renderFbo {0};
        GLuint _readFbo {0};
        Texture_ImagePtr _depthTexture {nullptr};
        Texture_ImagePtr _colorTexture {nullptr};
        GLsync _renderFence;

        ObjectPtr _screen;
        ObjectPtr _screenGui;
        glm::dmat4 _viewProjectionMatrix;
        std::vector<TexturePtr> _inTextures;
        TexturePtr _guiTexture {nullptr}; // The gui has its own texture

        static std::mutex _callbackMutex;
        static std::deque<std::pair<GLFWwindow*, std::vector<int>>> _keys; // Input keys queue
        static std::deque<std::pair<GLFWwindow*, unsigned int>> _chars; // Input keys queue
        static std::deque<std::pair<GLFWwindow*, std::vector<int>>> _mouseBtn; // Input mouse buttons queue
        static std::pair<GLFWwindow*, std::vector<double>> _mousePos; // Input mouse position
        static std::deque<std::pair<GLFWwindow*, std::vector<double>>> _scroll; // Input mouse scroll queue
        static std::atomic_bool _quitFlag; // Grabs close window events

        // Swapping loop
        void swapLoop();

        /**
         * Input callbacks
         */
        static void keyCallback(GLFWwindow* win, int key, int scancode, int action, int mods);
        static void charCallback(GLFWwindow* win, unsigned int codepoint);
        static void mouseBtnCallback(GLFWwindow* win, int button, int action, int mods);
        static void mousePosCallback(GLFWwindow* win, double xpos, double ypos);
        static void scrollCallback(GLFWwindow* win, double xoffset, double yoffset);
        static void closeCallback(GLFWwindow* win);

        /**
         * Set FBOs up
         */
        void setupRenderFBO();
        void setupReadFBO();

        /**
         * Register new functors to modify attributes
         */
        void registerAttributes();

        /**
         * Set up the user events callbacks
         */
        void setEventsCallbacks();

        /**
         * Set up the projection surface
         */
        bool setProjectionSurface();

        /**
         * Set whether the window has decorations
         */
        void setWindowDecoration(bool hasDecoration);

        /**
         * Update the swap interval
         */
        void updateSwapInterval();

        /**
         * Update the window size and position
         */
        void updateWindowShape();
};

typedef std::shared_ptr<Window> WindowPtr;

} // end of namespace

#endif // SPLASH_WINDOW_H
