#ifndef		HAS_MESH_H_BEEN_INCLUDED
#define		HAS_MESH_H_BEEN_INCLUDED

#include	"ng_mesh_simplify.h"

#include	<vector>
#include	<GL/glew.h>
#include	<SDL_opengl.h>

// ----------------------------------------------------------------------------

class Mesh
{
public:

	void initialise();
	void uploadData(const MeshBuffer* buffer);
	void destroy();

	GLuint vertexArrayObj_ = 0;
	GLuint vertexBuffer_ = 0;
	GLuint indexBuffer_ = 0;
	int	numIndices_ = 0;
};

// ----------------------------------------------------------------------------

MeshBuffer* LoadMeshFromFile(const std::string& filename);

// ----------------------------------------------------------------------------

#endif	//	HAS_MESH_H_BEEN_INCLUDED
