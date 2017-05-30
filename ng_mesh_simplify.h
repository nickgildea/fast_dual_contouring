#ifndef		HAS_SIMD_MESH_SIMPLIFY_H_BEEN_INCUDED
#define		HAS_SIMD_MESH_SIMPLIFY_H_BEEN_INCUDED

//
// Release under unlicense, public domain
//

#include <stdlib.h>
#include <glm/glm.hpp>

// ----------------------------------------------------------------------------

// Change these definitions to suit your project
#if 0
using vec4 = float[4];
#else
using vec4 = glm::vec4;
#endif

// ----------------------------------------------------------------------------

// The algorithm with alloc chunks of memory
// override these to point to your engine's allocators, etc
#define ng_alloc malloc
#define ng_free free

// ----------------------------------------------------------------------------

struct MeshSimplificationOptions
{
	// Each iteration involves selecting a fraction of the edges at random as possible 
	// candidates for collapsing. There is likely a sweet spot here trading off against number 
	// of edges processed vs number of invalid collapses generated due to collisions 
	// (the more edges that are processed the higher the chance of collisions happening)
	float edgeFraction = 0.125f;

	// Stop simplfying after a given number of iterations
	int maxIterations = 10;

	// And/or stop simplifying when we've reached a percentage of the input triangles
	float targetPercentage = 0.05f;

	// The maximum allowed error when collapsing an edge (error is calculated as 1.0/qef_error)
	float maxError = 1.f;

	// Useful for controlling how uniform the mesh is (or isn't)
	float maxEdgeSize = 0.5f;

	// If the mesh has sharp edges this can used to prevent collapses which would otherwise be used
	float minAngleCosine = 0.8f;
};

// ----------------------------------------------------------------------------

struct MeshVertex
{
	vec4 xyz, normal, colour;
};

// ----------------------------------------------------------------------------

struct MeshTriangle
{
	int indices_[3];
};

// ----------------------------------------------------------------------------

class MeshBuffer
{
public:

	static void initialiseVertexArray();

	MeshVertex*   vertices = nullptr;
	int           numVertices = 0;

	MeshTriangle* triangles = nullptr;
	int           numTriangles = 0;
};

// ----------------------------------------------------------------------------

// The MeshBuffer instance will be edited in place
void ngMeshSimplifier(
	MeshBuffer* mesh,
	const vec4& worldSpaceOffset,
	const MeshSimplificationOptions& options);

// ----------------------------------------------------------------------------

#endif	//	HAS_SIMD_MESH_SIMPLIFY_H_BEEN_INCUDED

