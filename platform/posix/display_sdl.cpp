// The display as an SDL3 window.  (display_none.cpp is linked instead where
// SDL3 is not installed.)
#include "../../core/platform.h"

#include <SDL3/SDL.h>

namespace basic {

namespace {

SDL_Window *window;
SDL_Renderer *renderer;
SDL_Texture *texture;
int fb_width;

int keys[64];                     // typed and not yet read; a ring
unsigned key_head, key_tail;
int flags;

void queue_key(int key)
{
    if(key_tail - key_head < 64) { keys[key_tail++ % 64] = key; }
}

void key_down(const SDL_KeyboardEvent &e)
{
    if(e.mod & SDL_KMOD_CTRL) {
        if(e.key == SDLK_C) {
            flags |= DISPLAY_BREAK;
        } else if(e.key >= SDLK_A && e.key <= SDLK_Z) {
            queue_key(static_cast<int>(e.key - SDLK_A) + 1);
        }
        return;
    }
    switch(e.key) {
        case SDLK_RETURN: case SDLK_KP_ENTER: queue_key(13); break;
        case SDLK_BACKSPACE: case SDLK_DELETE: case SDLK_LEFT: queue_key(8); break;
        case SDLK_RIGHT: queue_key(21); break;
        case SDLK_UP: queue_key(11); break;
        case SDLK_DOWN: queue_key(10); break;
        case SDLK_ESCAPE: queue_key(27); break;
        case SDLK_TAB: queue_key(9); break;
        default: break;                           // printable keys come as text
    }
}

}  // namespace

bool plat_display_open(int width, int height, const char *title)
{
    if(window) { return true; }
    if(!SDL_Init(SDL_INIT_VIDEO)) { return false; }
    // Twice the framebuffer's size to start with; it scales to fit after that.
    if(!SDL_CreateWindowAndRenderer(title, width * 2, height * 2, SDL_WINDOW_RESIZABLE,
            &window, &renderer)) {
        window = nullptr;
        return false;
    }
    SDL_SetRenderLogicalPresentation(renderer, width, height, SDL_LOGICAL_PRESENTATION_LETTERBOX);
    texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING,
        width, height);
    if(!texture) { return false; }
    SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST);
    SDL_StartTextInput(window);
    SDL_RaiseWindow(window);
    fb_width = width;
    return true;
}

void plat_display_show(const uint32_t *pixels)
{
    SDL_UpdateTexture(texture, nullptr, pixels, fb_width * 4);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    SDL_RenderTexture(renderer, texture, nullptr, nullptr);
    SDL_RenderPresent(renderer);
}

int plat_display_pump()
{
    SDL_Event e;
    while(SDL_PollEvent(&e)) {
        switch(e.type) {
            case SDL_EVENT_QUIT: case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
                flags |= DISPLAY_CLOSED;
                break;
            case SDL_EVENT_KEY_DOWN:
                key_down(e.key);
                break;
            case SDL_EVENT_TEXT_INPUT:
                for(const char *p = e.text.text; *p; p++) {
                    if(*p >= 32 && *p < 127) { queue_key(*p); }
                }
                break;
            default:
                break;
        }
    }
    int result = flags;
    flags = 0;
    return result;
}

int plat_display_key()
{
    return key_head != key_tail ? keys[key_head++ % 64] : -1;
}

}  // namespace basic
