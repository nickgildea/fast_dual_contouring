#ifndef		HAS_IMGUI_HANDLERS_H_BEEN_INCLUDED
#define		HAS_IMGUI_HANDLERS_H_BEEN_INCLUDED

#include	"sdl_wrapper.h"
#include	<imgui.h>

#if 1

// ImGui SDL2 binding with OpenGL
// https://github.com/ocornut/imgui

struct SDL_Window;
typedef union SDL_Event SDL_Event;

IMGUI_API bool        ImGui_ImplSdl_Init(SDL_Window *window);
IMGUI_API void        ImGui_ImplSdl_Shutdown();
IMGUI_API void        ImGui_ImplSdl_NewFrame(SDL_Window *window);
IMGUI_API bool        ImGui_ImplSdl_ProcessEvent(SDL_Event* event);

// Use if you want to reset your rendering device without losing ImGui state.
IMGUI_API void        ImGui_ImplSdl_InvalidateDeviceObjects();
IMGUI_API bool        ImGui_ImplSdl_CreateDeviceObjects();

#else
bool ImGui_Init(SDL_Window* window, bool install_callbacks);
void ImGui_Shutdown();
void ImGui_NewFrame();
void ImGui_MouseButtonCallback(SDL_Window* window, const SDL_MouseButtonEvent& event);
void ImGui_ScrollCallback(SDL_Window* window, const SDL_MouseWheelEvent& event);
void ImGui_KeyCallback(SDL_Window* window, const SDL_KeyboardEvent& event);
void ImGui_CharCallback(SDL_Window* window, unsigned int c);
#endif

#endif	//	HAS_IMGUI_HANDLERS_H_BEEN_INCLUDED