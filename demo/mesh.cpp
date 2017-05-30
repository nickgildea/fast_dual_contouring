#include	"mesh.h"

#include	<stdio.h>
#include	<algorithm>
#include	<fstream>

#include	<glm/ext.hpp>
using namespace glm;

// ----------------------------------------------------------------------------

void Mesh::initialise()
{
	glGenVertexArrays(1, &vertexArrayObj_);
	glGenBuffers(1, &vertexBuffer_);
	glGenBuffers(1, &indexBuffer_);

	glBindVertexArray(vertexArrayObj_);
	glBindBuffer(GL_ARRAY_BUFFER, vertexBuffer_);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, indexBuffer_);

	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, sizeof(MeshVertex), 0);
	
	glEnableVertexAttribArray(1);
	glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(MeshVertex), (void*)(sizeof(glm::vec4) * 1));
	
	glEnableVertexAttribArray(2);
	glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(MeshVertex), (void*)(sizeof(glm::vec4) * 1));

	glBindVertexArray(0);
}

// ----------------------------------------------------------------------------

void Mesh::uploadData(const MeshBuffer* buffer)
{
	if (!buffer)
	{
		return;
	}

	glBindVertexArray(vertexArrayObj_);

	glBindBuffer(GL_ARRAY_BUFFER, vertexBuffer_);
	glBufferData(GL_ARRAY_BUFFER, sizeof(MeshVertex) * buffer->numVertices, &buffer->vertices[0], GL_STATIC_DRAW);

	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, indexBuffer_);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(MeshTriangle) * buffer->numTriangles, &buffer->triangles[0], GL_STATIC_DRAW);

	printf("Mesh: %d vertices\n", buffer->numVertices);
	numIndices_ = 3 * buffer->numTriangles;

	glBindVertexArray(0);
}

// ----------------------------------------------------------------------------

void Mesh::destroy()
{
	glDeleteBuffers(1, &indexBuffer_);
	glDeleteBuffers(1, &vertexBuffer_);
	glDeleteVertexArrays(1, &vertexArrayObj_);
}

