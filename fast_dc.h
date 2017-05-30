#ifndef 	HAS_DC_H_BEEN_INCLUDED
#define		HAS_DC_H_BEEN_INCLUDED

#include	"ng_mesh_simplify.h"

struct SuperPrimitiveConfig
{
	enum Type
	{
		Cube,
		Cylinder,
		Pill,
		Corridor,
		Torus,
	};

	glm::vec4 s;
	glm::vec2 r;
};

SuperPrimitiveConfig ConfigForShape(const SuperPrimitiveConfig::Type& type);
MeshBuffer* GenerateMesh(const SuperPrimitiveConfig& config);

#endif //	HAS_DC_H_BEEN_INCLUDED