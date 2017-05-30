//
// Public domain
//

#include	"ng_mesh_simplify.h"

#define QEF_INCLUDE_IMPL
#include	"qef_simd.h"

#include	<stdint.h>
#include	<algorithm>
#include	<random>

// ----------------------------------------------------------------------------

namespace {

const int COLLAPSE_MAX_DEGREE = 16;
const int MAX_TRIANGLES_PER_VERTEX = COLLAPSE_MAX_DEGREE;

template <typename T>
class LinearBuffer
{
public:

	LinearBuffer(const int capacity)
	{
		base_ = static_cast<T*>(ng_alloc(sizeof(T) * capacity));
		end_ = base_ + capacity;
	}

	~LinearBuffer()
	{
		ng_free(base_);
	}

	void swap(LinearBuffer<T>& other)
	{
		std::swap(base_, other.base_);
		std::swap(end_, other.end_);
		std::swap(size_, other.size_);
	}

	void clear()
	{
		size_ = 0;
	}

	int size() const
	{
		return size_;
	}

	void copy(const T* data, const int count)
	{
		memcpy(base_, data, sizeof(T) * count);
		size_ = count;
	}

	void resize(const int size, const T& value)
	{
		size_ = size;
		for (int i = 0; i < size; i++)
		{
			base_[i] = value;
		}
	}

	void push_back(const T& value)
	{
		base_[size_++] = value;
	}

	T* begin() const { return base_; }
	T* end() const { return base_ + size_; }

	const T& operator[](const int idx) const { return base_[idx]; }
	T& operator[](const int idx) { return base_[idx]; }


private:

	LinearBuffer() = delete;
	LinearBuffer(const LinearBuffer&) = delete;
	LinearBuffer(LinearBuffer&&) = delete;
	LinearBuffer& operator=(const LinearBuffer&) = delete;
	LinearBuffer& operator=(LinearBuffer&&) = delete;

	T* base_ = nullptr;
	T* end_ = nullptr;
	int size_ = 0;
};

template <typename T>
T* begin(const LinearBuffer<T>& buf)
{
	return buf.begin();
}

template <typename T>
T* end(const LinearBuffer<T>& buf)
{
	return buf.end();
}

// ----------------------------------------------------------------------------

union Edge
{
	Edge(uint32_t _minIndex, uint32_t _maxIndex)
		: min_(_minIndex)
		, max_(_maxIndex)
	{
	}

	uint64_t idx_ = 0;
	struct { uint32_t min_, max_; };
};

}

// ----------------------------------------------------------------------------

static inline int min(const int x, const int y)
{
	return x < y ? x : y;
}

static inline int max(const int x, const int y)
{
	return x > y ? x : y;
}

static inline void vec4_set(vec4& r, const vec4& x)
{
	r[0] = x[0];
	r[1] = x[1];
	r[2] = x[2];
	r[3] = x[3];
}

static inline void vec4_add(vec4& r, const vec4& x, const vec4& y)
{
	r[0] = x[0] + y[0];
	r[1] = x[1] + y[1];
	r[2] = x[2] + y[2];
	r[3] = x[3] + y[3];
}

static inline void vec4_sub(vec4& r, const vec4& x, const vec4& y)
{
	r[0] = x[0] - y[0];
	r[1] = x[1] - y[1];
	r[2] = x[2] - y[2];
	r[3] = x[3] - y[3];
}

static inline void vec4_scale(vec4& r, const float scale)
{
	r[0] *= scale;
	r[1] *= scale;
	r[2] *= scale;
	r[3] *= scale;
}

static inline float vec4_dot(const vec4& x, const vec4& y)
{
	return x[0] * y[0] + x[1] * y[1] + x[2] * y[2] + x[3] * y[3];
}

static inline float vec4_length2(const vec4& x)
{
	return vec4_dot(x, x);
}

// ----------------------------------------------------------------------------

static void BuildCandidateEdges(
	const LinearBuffer<MeshVertex>& vertices,
	const LinearBuffer<MeshTriangle>& triangles,
	LinearBuffer<Edge>& edges)
{
	for (int i = 0; i < triangles.size(); i++)
	{
		const int* indices = triangles[i].indices_;
		edges.push_back(Edge(min(indices[0], indices[1]), max(indices[0], indices[1])));
		edges.push_back(Edge(min(indices[1], indices[2]), max(indices[1], indices[2])));
		edges.push_back(Edge(min(indices[0], indices[2]), max(indices[0], indices[2])));
	}

	std::sort(begin(edges), end(edges), 
		[](const Edge& lhs, const Edge& rhs) { return lhs.idx_ < rhs.idx_; });

	LinearBuffer<Edge> filteredEdges(edges.size());
	LinearBuffer<bool> boundaryVerts(vertices.size());
	boundaryVerts.resize(vertices.size(), false);

	Edge prev = edges[0];
	int count = 1;
	for (int idx = 1; idx < edges.size(); idx++)
	{
		const Edge curr = edges[idx];
		if (curr.idx_ != prev.idx_)
		{
			if (count == 1)
			{
				boundaryVerts[prev.min_] = true;
				boundaryVerts[prev.max_] = true;
			}
			else 
			{
				filteredEdges.push_back(prev);
			}

			count = 1;
		}
		else
		{
			count++;
		}

		prev = curr;
	}

	edges.clear();
	for (Edge& edge: filteredEdges)
	{
		if (!boundaryVerts[edge.min_] && !boundaryVerts[edge.max_])
		{
			edges.push_back(edge);
		}
	}
}

// ----------------------------------------------------------------------------

static int FindValidCollapses(
	const MeshSimplificationOptions& options,
	const LinearBuffer<Edge>& edges,
	const LinearBuffer<MeshVertex>& vertices,
	const LinearBuffer<MeshTriangle>& tris,
	const LinearBuffer<int>& vertexTriangleCounts,
	LinearBuffer<int>& collapseValid, 
	LinearBuffer<int>& collapseEdgeID, 
	LinearBuffer<vec4>& collapsePosition,
	LinearBuffer<vec4>& collapseNormal)
{
	int validCollapses = 0;

	std::mt19937 prng;
	prng.seed(42);

	const int numRandomEdges = edges.size() * options.edgeFraction;
	std::uniform_int_distribution<int> distribution(0, (int)(edges.size() - 1));

	LinearBuffer<int> randomEdges(numRandomEdges);
	for (int i = 0; i < numRandomEdges; i++)
	{
		const int randomIdx = distribution(prng);
		randomEdges.push_back(randomIdx);
	}

	// sort the edges to improve locality
	std::sort(begin(randomEdges), end(randomEdges));

	LinearBuffer<float> minEdgeCost(vertices.size());
	minEdgeCost.resize(vertices.size(), FLT_MAX);

	for (int i: randomEdges)
	{
		const Edge& edge = edges[i];
		const auto& vMin = vertices[edge.min_];
		const auto& vMax = vertices[edge.max_];

		// prevent collapses along edges
		const float cosAngle = vec4_dot(vMin.normal, vMax.normal);
		if (cosAngle < options.minAngleCosine)
		{
			continue;
		}

		vec4 delta;
		vec4_sub(delta, vMax.xyz, vMin.xyz);
		const float edgeSize = vec4_length2(delta);
		if (edgeSize > (options.maxEdgeSize * options.maxEdgeSize))
		{
			continue;
		}

		const int degree = vertexTriangleCounts[edge.min_] + vertexTriangleCounts[edge.max_];
		if (degree > COLLAPSE_MAX_DEGREE)
		{
			continue;
		}

		__declspec(align(16)) float pos[4];
		MeshVertex data[2] = { vMin, vMax };

		float error = qef_solve_from_points_4d_interleaved(&data[0].xyz[0], sizeof(MeshVertex) / sizeof(float), 2, pos);
		if (error > 0.f)
		{
			error = 1.f / error;
		}

		// avoid vertices becoming a 'hub' for lots of edges by penalising collapses
		// which will lead to a vertex with degree > 10
		const int penalty = max(0, degree - 10);
		error += penalty * (options.maxError * 0.1f);

		if (error > options.maxError)
		{
			continue;
		}

		collapseValid.push_back(i);

		vec4_add(collapseNormal[i], vMin.normal, vMax.normal);
		vec4_scale(collapseNormal[i], 0.5f);

		vec4_set(collapsePosition[i], vec4(pos[0], pos[1], pos[2], 1.f));

		if (error < minEdgeCost[edge.min_])
		{
			minEdgeCost[edge.min_] = error;
			collapseEdgeID[edge.min_] = i;
		}

		if (error < minEdgeCost[edge.max_])
		{
			minEdgeCost[edge.max_] = error;
			collapseEdgeID[edge.max_] = i;
		}

		validCollapses++;
	}

	return validCollapses;
}

// ----------------------------------------------------------------------------

static void CollapseEdges(
	const LinearBuffer<int>& collapseValid,
	const LinearBuffer<Edge>& edges,
	const LinearBuffer<int>& collapseEdgeID,
	const LinearBuffer<vec4>& collapsePositions,
	const LinearBuffer<vec4>& collapseNormal,
	LinearBuffer<MeshVertex>& vertices,
	LinearBuffer<int>& collapseTarget)
{
	int countCollapsed = 0, countCandidates = 0;
	for (int i: collapseValid)
	{
		countCandidates++;

		const Edge& edge = edges[i];
		if (collapseEdgeID[edge.min_] == i && collapseEdgeID[edge.max_] == i)
		{
			countCollapsed++;

			collapseTarget[edge.max_] = edge.min_;
			vec4_set(vertices[edge.min_].xyz, collapsePositions[i]);
			vec4_set(vertices[edge.min_].normal, collapseNormal[i]);
		}
	}
}

// ----------------------------------------------------------------------------

static int RemoveTriangles(
	const LinearBuffer<MeshVertex>& vertices,
	const LinearBuffer<int>& collapseTarget,
	LinearBuffer<MeshTriangle>& tris,
	LinearBuffer<MeshTriangle>& triBuffer,
	LinearBuffer<int>& vertexTriangleCounts)
{
	int removedCount = 0;

	vertexTriangleCounts.clear();
	vertexTriangleCounts.resize(vertices.size(), 0);

	triBuffer.clear();

	for (auto& tri: tris)
	{
		for (int j = 0; j < 3; j++)
		{
			const int t = collapseTarget[tri.indices_[j]];
			if (t != -1)
			{
				tri.indices_[j] = t;
			}
		}

		if (tri.indices_[0] == tri.indices_[1] ||
			tri.indices_[0] == tri.indices_[2] ||
			tri.indices_[1] == tri.indices_[2])
		{
			removedCount++;

			continue;
		}

		const int* indices = tri.indices_;
		for (int index = 0; index < 3; index++)
		{
			vertexTriangleCounts[indices[index]] += 1;
		}

		triBuffer.push_back(tri);
	}

	tris.swap(triBuffer);
	return removedCount;
}

// ----------------------------------------------------------------------------

static void RemoveEdges(
	const LinearBuffer<int>& collapseTarget,
	LinearBuffer<Edge>& edges,
	LinearBuffer<Edge>& edgeBuffer)
{
	edgeBuffer.clear();
	for (auto& edge: edges)
	{
		int t = collapseTarget[edge.min_];
		if (t != -1)
		{
			edge.min_ = t;
		}

		t = collapseTarget[edge.max_];
		if (t != -1)
		{
			edge.max_ = t;
		}

		if (edge.min_ != edge.max_)
		{
			edgeBuffer.push_back(edge);
		}
	}

	edges.swap(edgeBuffer);
}			

// ----------------------------------------------------------------------------

static void CompactVertices(
	LinearBuffer<MeshVertex>& vertices,
	MeshBuffer* meshBuffer)
{
	LinearBuffer<bool> vertexUsed(vertices.size());
	vertexUsed.resize(vertices.size(), false);

	for (int i = 0; i < meshBuffer->numTriangles; i++)
	{
		MeshTriangle& tri = meshBuffer->triangles[i];

		vertexUsed[tri.indices_[0]] = true;
		vertexUsed[tri.indices_[1]] = true;
		vertexUsed[tri.indices_[2]] = true;
	}

	LinearBuffer<MeshVertex> compactVertices(vertices.size());
	LinearBuffer<int> remappedVertexIndices(vertices.size());
	remappedVertexIndices.resize(vertices.size(), -1);

	for (int i = 0; i < vertices.size(); i++)
	{
		if (vertexUsed[i])
		{
			remappedVertexIndices[i] = compactVertices.size();
			vec4_set(vertices[i].normal, vertices[i].normal);
			compactVertices.push_back(vertices[i]);
		}
	}

	for (int i = 0; i < meshBuffer->numTriangles; i++)
	{
		MeshTriangle& tri = meshBuffer->triangles[i];
		
		for (int j = 0; j < 3; j++)
		{
			const int updatedIndex = remappedVertexIndices[tri.indices_[j]];
			tri.indices_[j] = updatedIndex;
		}
	}

	vertices.swap(compactVertices);
}

// ----------------------------------------------------------------------------

void ngMeshSimplifier(
	MeshBuffer* mesh,
	const vec4& worldSpaceOffset,
	const MeshSimplificationOptions& options)
{
	if (mesh->numTriangles < 100 || mesh->numVertices < 100)
	{
		return;
	}

	LinearBuffer<MeshVertex> vertices(mesh->numVertices);
	vertices.copy(&mesh->vertices[0], mesh->numVertices);

	LinearBuffer<MeshTriangle> triangles(mesh->numTriangles);
	triangles.copy(&mesh->triangles[0], mesh->numTriangles);

	for (MeshVertex& v: vertices)
	{
		vec4_sub(v.xyz, v.xyz, worldSpaceOffset);
	}

	mesh->numVertices = 0;
	mesh->numTriangles = 0;

	LinearBuffer<Edge> edges(triangles.size() * 3);
	BuildCandidateEdges(vertices, triangles, edges);

	LinearBuffer<vec4> collapsePosition(edges.size());
	LinearBuffer<vec4> collapseNormal(edges.size());
	LinearBuffer<int> collapseValid(edges.size());
	LinearBuffer<int> collapseEdgeID(vertices.size());
	LinearBuffer<int> collapseTarget(vertices.size());

	LinearBuffer<Edge> edgeBuffer(edges.size());
	LinearBuffer<MeshTriangle> triBuffer(triangles.size());

	// per vertex
	LinearBuffer<int> vertexTriangleCounts(vertices.size());
	vertexTriangleCounts.resize(vertices.size(), 0);

	{
		for (int j = 0; j < triangles.size(); j++)
		{
			const int* indices = triangles[j].indices_;

			for (int index = 0; index < 3; index++)
			{
				vertexTriangleCounts[indices[index]] += 1;
			}
		}
	}

	const int targetTriangleCount = triangles.size() * options.targetPercentage;

	int iterations = 0;
	while (triangles.size() > targetTriangleCount &&
	       iterations++ < options.maxIterations)
	{
		collapseEdgeID.resize(vertices.size(), -1);
		collapseTarget.resize(vertices.size(), -1);

		collapseValid.clear();

		const int countValidCollapse = FindValidCollapses(
			options,
			edges, vertices, triangles, vertexTriangleCounts, collapseValid, 
			collapseEdgeID, collapsePosition, collapseNormal);
		if (countValidCollapse == 0)
		{
			break;
		}

		CollapseEdges(collapseValid, edges,
			collapseEdgeID, collapsePosition, collapseNormal, vertices, 
			collapseTarget);

		RemoveTriangles(vertices, collapseTarget, triangles, triBuffer, vertexTriangleCounts);
		RemoveEdges(collapseTarget, edges, edgeBuffer);
	}

	mesh->numTriangles = 0;
	for (int i = 0; i < triangles.size(); i++)
	{
		mesh->triangles[mesh->numTriangles].indices_[0] = triangles[i].indices_[0];
		mesh->triangles[mesh->numTriangles].indices_[1] = triangles[i].indices_[1];
		mesh->triangles[mesh->numTriangles].indices_[2] = triangles[i].indices_[2];
		mesh->numTriangles++;
	}

	CompactVertices(vertices, mesh);

	mesh->numVertices = vertices.size();
	for (int i = 0; i < vertices.size(); i++)
	{
		vec4_add(vertices[i].xyz, vertices[i].xyz, worldSpaceOffset);

		vec4_set(mesh->vertices[i].xyz, vertices[i].xyz);
		vec4_set(mesh->vertices[i].normal, vertices[i].normal);
		vec4_set(mesh->vertices[i].colour, vertices[i].colour);
	}
}

