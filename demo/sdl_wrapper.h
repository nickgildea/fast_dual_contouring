#ifndef		HAS_SDL_WRAPPER_H_BEEN_INCLUDED
#define		HAS_SDL_WRAPPER_H_BEEN_INCLUDED

#include	<GL/glew.h>
#include	<SDL.h>
#include	<SDL_syswm.h>
#include	<SDL_opengl.h>

// these are #define'd and not undef'd which causes problems with cl.hpp
#undef __SSE__
#undef __SSE2__

// also left defined
#undef near
#undef far

#endif	//	HAS_SDL_WRAPPER_H_BEEN_INCLUDED
