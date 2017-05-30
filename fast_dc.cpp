//
// Written by Nick Gildea (2017)
// Public Domain
//

#include "fast_dc.h"

#include "ng_mesh_simplify.h"
#include "qef_simd.h"

#include <glm/glm.hpp>
#include <stdint.h>

// ----------------------------------------------------------------------------

// TODO the winding field could be packed into the normal vec as the unused w component
struct EdgeInfo
{
	vec4 pos;
	vec4 normal;
	bool winding = false;
};

// Ideally we'd use https://github.com/greg7mdp/sparsepp but fall back to STL
#ifdef HAVE_SPARSEPP

	#include <sparsepp/spp.h>

	using EdgeInfoMap = spp::sparese_hash_map<uint32_t, EdgeInfo>;
	using VoxelIDSet = spp::sparse_hash_set<uint32_t>;
	using VoxelIndexMap = spp::sparese_hash_map<uint32_t, int>;

#else

	#include <unordered_map>
	#include <unordered_set>

	using EdgeInfoMap = std::unordered_map<uint32_t, EdgeInfo>;
	using VoxelIDSet = std::unordered_set<uint32_t>;
	using VoxelIndexMap = std::unordered_map<uint32_t, int>;

#endif

// ----------------------------------------------------------------------------

using glm::ivec4;
using glm::vec4;
using glm::vec3;
using glm::vec2;

#ifdef _MSC_VER
#define ALIGN16 __declspec(align(16))
#else
#define ALIGN16 __attribute__((aligned(16))
#endif

// ----------------------------------------------------------------------------

const int VOXEL_GRID_SIZE = 128;
const float VOXEL_GRID_OFFSET = (float)VOXEL_GRID_SIZE / 2.f;

// ----------------------------------------------------------------------------

static const vec4 AXIS_OFFSET[3] = 
{ 
	vec4(1.f, 0.f, 0.f, 0.f),
	vec4(0.f, 1.f, 0.f, 0.f),
	vec4(0.f, 0.f, 1.f, 0.f)
};

// ----------------------------------------------------------------------------

static const ivec4 EDGE_NODE_OFFSETS[3][4] = 
{
	{ ivec4(0), ivec4(0, 0, 1, 0), ivec4(0, 1, 0, 0), ivec4(0, 1, 1, 0) },
	{ ivec4(0), ivec4(1, 0, 0, 0), ivec4(0, 0, 1, 0), ivec4(1, 0, 1, 0) },
	{ ivec4(0), ivec4(0, 1, 0, 0), ivec4(1, 0, 0, 0), ivec4(1, 1, 0, 0) },
};

// ----------------------------------------------------------------------------

// The two lookup tables below were calculated by expanding the IDs into 3d coordinates
// performing the calcuations in 3d space and then converting back into the compact form 
// and subtracting the base voxel ID. Use of this lookup table means those calculations 
// can be avoided at run-time.

const uint32_t ENCODED_EDGE_NODE_OFFSETS[12] =
{
	0x00000000,
	0x00100000,
	0x00000400,
	0x00100400,
	0x00000000,
	0x00000001,
	0x00100000,
	0x00100001,
	0x00000000,
	0x00000400,
	0x00000001,
	0x00000401,
};

const uint32_t ENCODED_EDGE_OFFSETS[12] =
{
	0x00000000,
	0x00100000,
	0x00000400,
	0x00100400,
	0x40000000,
	0x40100000,
	0x40000001,
	0x40100001,
	0x80000000,
	0x80000400,
	0x80000001,
	0x80000401,
};

// ----------------------------------------------------------------------------

// The "super primitve" -- use the parameters to configure different shapes from a single function
// see https://www.shadertoy.com/view/MsVGWG

float sdSuperprim(vec3 p, vec4 s, vec2 r) 
{
	const vec3 d = glm::abs(p) - vec3(s);

	float q = glm::length(vec2(glm::max(d.x + r.x, 0.f), glm::max(d.y + r.x, 0.f)));
	q += glm::min(-r.x, glm::max(d.x,d.y));
    q = (glm::abs((q + s.w)) - s.w);

    return glm::length(vec2(glm::max(q + r.y,0.f), 
		glm::max(d.z + r.y, 0.f))) + glm::min(-r.y, glm::max(q, d.z));
}

// ----------------------------------------------------------------------------

float Density(const SuperPrimitiveConfig& config, const vec4& p)
{
	const float scale = 32.f;
	return sdSuperprim(vec3(p) / scale, vec4(config.s), vec2(config.r)) * scale;
}

// ----------------------------------------------------------------------------

uint32_t EncodeVoxelUniqueID(const ivec4& idxPos)
{
	return idxPos.x | (idxPos.y << 10) | (idxPos.z << 20);
}

// ----------------------------------------------------------------------------

ivec4 DecodeVoxelUniqueID(const uint32_t id)
{
	return ivec4(
		id & 0x3ff,
		(id >> 10) & 0x3ff,
		(id >> 20) & 0x3ff,
		0);
}

// ----------------------------------------------------------------------------

uint32_t EncodeAxisUniqueID(const int axis, const int x, const int y, const int z)
{
	return (x << 0) | (y << 10) | (z << 20) | (axis << 30);
}

// ----------------------------------------------------------------------------

float FindIntersection(const SuperPrimitiveConfig& config, const vec4& p0, const vec4& p1)
{
	const int FIND_EDGE_INFO_STEPS = 16;
	const float FIND_EDGE_INFO_INCREMENT = 1.f / FIND_EDGE_INFO_STEPS;

	float minValue = FLT_MAX;
	float currentT = 0.f;
	float t = 0.f;
	for (int i = 0; i < FIND_EDGE_INFO_STEPS; i++)
	{
		const vec4 p = glm::mix(p0, p1, currentT);
		const float	d = glm::abs(Density(config, p));
		if (d < minValue)
		{
			t = currentT;
			minValue = d;
		}

		currentT += FIND_EDGE_INFO_INCREMENT;
	}

	return t;
}

// ----------------------------------------------------------------------------

static void FindActiveVoxels(
	const SuperPrimitiveConfig& config,
	VoxelIDSet& activeVoxels,
	EdgeInfoMap& activeEdges)
{
	for (int x = 0; x < VOXEL_GRID_SIZE; x++)
	for (int y = 0; y < VOXEL_GRID_SIZE; y++)
	for (int z = 0; z < VOXEL_GRID_SIZE; z++)
	{
		const ivec4 idxPos(x, y, z, 0);
		const vec4 p = vec4(x - VOXEL_GRID_OFFSET, y - VOXEL_GRID_OFFSET, z - VOXEL_GRID_OFFSET, 1.f);

		for (int axis = 0; axis < 3; axis++)
		{
			const vec4 q = p + AXIS_OFFSET[axis];

			const float pDensity = Density(config, p);
			const float qDensity = Density(config, q);

			const bool zeroCrossing = 
				pDensity >= 0.f && qDensity < 0.f ||
			    pDensity < 0.f && qDensity >= 0.f;
			if (!zeroCrossing)
			{
				continue;
			}

			const float t = FindIntersection(config, p, q);
			const vec4 pos = vec4(glm::mix(glm::vec3(p), glm::vec3(q), t), 1.f);

			const float H = 0.001f;
			const auto normal = glm::normalize(vec4(
				Density(config, pos + vec4(H, 0.f, 0.f, 0.f)) - Density(config, pos - vec4(H, 0.f, 0.f, 0.f)), 
				Density(config, pos + vec4(0.f, H, 0.f, 0.f)) - Density(config, pos - vec4(0.f, H, 0.f, 0.f)), 
				Density(config, pos + vec4(0.f, 0.f, H, 0.f)) - Density(config, pos - vec4(0.f, 0.f, H, 0.f)), 
				0.f));

			EdgeInfo info;
			info.pos = pos;
			info.normal = normal;
			info.winding = pDensity >= 0.f;

			const auto code = EncodeAxisUniqueID(axis, x, y, z);
			activeEdges[code] = info;

			const auto edgeNodes = EDGE_NODE_OFFSETS[axis];
			for (int i = 0; i < 4; i++)
			{
				const auto nodeIdxPos = idxPos - edgeNodes[i];
				const auto nodeID = EncodeVoxelUniqueID(nodeIdxPos);
				activeVoxels.insert(nodeID);
			}
		}
	}
}

// ----------------------------------------------------------------------------

static void GenerateVertexData(
	const VoxelIDSet& voxels,
	const EdgeInfoMap& edges,
	VoxelIndexMap& vertexIndices,
	MeshBuffer* buffer)
{
	MeshVertex* vert = &buffer->vertices[0];

	int idxCounter = 0;
	for (const auto& voxelID: voxels)
	{
		ALIGN16 vec4 p[12];
		ALIGN16 vec4 n[12];

		int idx = 0;
		for (int i = 0; i < 12; i++)
		{
			const auto edgeID = voxelID + ENCODED_EDGE_OFFSETS[i];				
			const auto iter = edges.find(edgeID);

			if (iter != end(edges))
			{
				const auto& info = iter->second;
				const vec4 pos = info.pos;
				const vec4 normal = info.normal;

				p[idx] = pos;
				n[idx] = normal;
				idx++;
			}
		}

		ALIGN16 vec4 nodePos;
		qef_solve_from_points_4d(&p[0].x, &n[0].x, idx, &nodePos.x);

		vec4 nodeNormal;
		for (int i = 0; i < idx; i++)
		{
			nodeNormal += n[i];
		}
		nodeNormal *= (1.f / (float)idx);

		vertexIndices[voxelID] = idxCounter++;

		buffer->numVertices++;
		vert->xyz = nodePos;
		vert->normal = nodeNormal;
		vert++;
	}

}

// ----------------------------------------------------------------------------

static void GenerateTriangles(
	const EdgeInfoMap& edges,
	const VoxelIndexMap& vertexIndices,
	MeshBuffer* buffer)
{
	MeshTriangle* tri = &buffer->triangles[0];

	for (const auto& pair: edges)
	{
		const auto& edge = pair.first;
		const auto& info = pair.second;

		const ivec4 basePos = DecodeVoxelUniqueID(edge);
		const int axis = (edge >> 30) & 0xff;

		const int nodeID = edge & ~0xc0000000;
		const uint32_t voxelIDs[4] = 
		{
			nodeID - ENCODED_EDGE_NODE_OFFSETS[axis * 4 + 0],
			nodeID - ENCODED_EDGE_NODE_OFFSETS[axis * 4 + 1],
			nodeID - ENCODED_EDGE_NODE_OFFSETS[axis * 4 + 2],
			nodeID - ENCODED_EDGE_NODE_OFFSETS[axis * 4 + 3],
		};

		// attempt to find the 4 voxels which share this edge
		int edgeVoxels[4];
		int numFoundVoxels = 0;
		for (int i = 0; i < 4; i++)
		{
			const auto iter = vertexIndices.find(voxelIDs[i]);
			if (iter != end(vertexIndices))
			{
				edgeVoxels[numFoundVoxels++] = iter->second;
			}
		}

		// we can only generate a quad (or two triangles) if all 4 are found
		if (numFoundVoxels < 4)
		{
			continue;
		}

		if (info.winding)
		{
			tri->indices_[0] = edgeVoxels[0];
			tri->indices_[1] = edgeVoxels[1];
			tri->indices_[2] = edgeVoxels[3];
			tri++;

			tri->indices_[0] = edgeVoxels[0];
			tri->indices_[1] = edgeVoxels[3];
			tri->indices_[2] = edgeVoxels[2];
			tri++;
		}
		else
		{
			tri->indices_[0] = edgeVoxels[0];
			tri->indices_[1] = edgeVoxels[3];
			tri->indices_[2] = edgeVoxels[1];
			tri++;

			tri->indices_[0] = edgeVoxels[0];
			tri->indices_[1] = edgeVoxels[2];
			tri->indices_[2] = edgeVoxels[3];
			tri++;
		}

		buffer->numTriangles += 2;
	}
}

// ----------------------------------------------------------------------------

MeshBuffer* GenerateMesh(const SuperPrimitiveConfig& config)
{
	VoxelIDSet activeVoxels;
	EdgeInfoMap activeEdges;

	FindActiveVoxels(config, activeVoxels, activeEdges);

	MeshBuffer* buffer = new MeshBuffer;
	buffer->vertices = (MeshVertex*)malloc(activeVoxels.size() * sizeof(MeshVertex));
	buffer->numVertices = 0;

	VoxelIndexMap vertexIndices;
	GenerateVertexData(activeVoxels, activeEdges, vertexIndices, buffer);

	buffer->triangles = (MeshTriangle*)malloc(2 * activeEdges.size() * sizeof(MeshTriangle));
	buffer->numTriangles = 0;
	GenerateTriangles(activeEdges, vertexIndices, buffer);

	printf("mesh: %d %d\n", buffer->numVertices, buffer->numTriangles);

	return buffer;
}

// ----------------------------------------------------------------------------

SuperPrimitiveConfig ConfigForShape(const SuperPrimitiveConfig::Type& type)
{
	SuperPrimitiveConfig config;
	switch (type)
	{
		default:
		case SuperPrimitiveConfig::Cube:
			config.s = vec4(1.f);
			config.r = vec2(0.f);
			break;
			
		case SuperPrimitiveConfig::Cylinder:
			config.s = vec4(1.f);
			config.r = vec2(1.f, 0.f);
			break;

		case SuperPrimitiveConfig::Pill:
			config.s = vec4(1.f, 1.f, 2.f, 1.);
			config.r = vec2(1.f);
			break;

		case SuperPrimitiveConfig::Corridor:
			config.s = vec4(1.f, 1.f, 1.f, 0.25f);
			config.r = vec2(0.1f);
			break;

		case SuperPrimitiveConfig::Torus:
			config.s = vec4(1.f, 1.f, 0.25f, 0.25f);
			config.r = vec2(1.f, 0.25f);
			break;
	}

	return config;
}

// ----------------------------------------------------------------------------
