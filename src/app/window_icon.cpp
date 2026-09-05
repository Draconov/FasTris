#include "window_icon.hpp"
#include "app_icon_data.hpp"

#include <SDL3/SDL.h>

namespace fasttris::app {

void setFasTrisWindowIcon(SDL_Window* window) {
#if defined(__EMSCRIPTEN__) || defined(__ANDROID__)
    (void)window;
#else
    if (!window) return;
    SDL_Surface* icon = SDL_CreateSurfaceFrom(
        kAppIconWidth,
        kAppIconHeight,
        SDL_PIXELFORMAT_RGBA32,
        const_cast<unsigned char*>(kAppIconRgba),
        kAppIconPitch);
    if (!icon) return;
    SDL_SetWindowIcon(window, icon);
    SDL_DestroySurface(icon);
#endif
}

} // namespace fasttris::app
