#include <stdio.h>
#include <stdlib.h>

#include <GL\glew.h>
#include <SDL.h>
#include <SDL_opengl.h>
#include <glm\glm.hpp>

#include "glsl_program.h"
#include "mesh.h"
#include "qef_simd.h"
#include "ng_mesh_simplify.h"

#include <imgui.h>
#include "imgui_handlers.h"

#include "fast_dc.h"

// ----------------------------------------------------------------------------

void HandleMouseMove(const SDL_MouseMotionEvent& e, float& rotateXAxis, float& rotateYAxis)
{
	if (e.state & SDL_BUTTON_LMASK)
	{
		rotateXAxis += (float)e.yrel * 0.5f;
		rotateYAxis += (float)e.xrel * 0.5f;

		rotateXAxis = glm::min(80.f, rotateXAxis);
		rotateXAxis = glm::max(-80.f, rotateXAxis);
	}
}

// ----------------------------------------------------------------------------

void HandleMouseWheel(const SDL_MouseWheelEvent& e, float& distance)
{
	distance += -e.y * 10.f;
}

// ----------------------------------------------------------------------------

void HandleKeyPress(const SDL_KeyboardEvent& e, bool& drawWireframe)
{
	if (e.type != SDL_KEYUP)
	{
		return;
	}

	switch (e.keysym.sym)
	{
	case SDLK_F1:
		drawWireframe = !drawWireframe;
		break;
	}
}

// ----------------------------------------------------------------------------

struct ViewerOptions
{
	float meshScale = 1.f;
	bool drawWireframe = false;
	bool refreshModel = false;
};

bool GUI_DrawFrame(
	SDL_Window* window, 
	ViewerOptions& viewerOpts, 
	MeshSimplificationOptions& options, 
	SuperPrimitiveConfig& primConfig)
{
	ImGui::GetIO().MouseDrawCursor = false;
	ImGui_ImplSdl_NewFrame(window);
	SDL_ShowCursor(1);

	{
		ImGui::Begin("Options");
		
		if (ImGui::CollapsingHeader("Mesh Simplification Options"))
		{
			ImGui::SliderFloat("Random Edge Fraction", &options.edgeFraction, 0.f, 1.f);
			ImGui::SliderInt("Max Iterations", &options.maxIterations, 1, 100);
			ImGui::SliderFloat("Target Triangle Percentage", &options.targetPercentage, 0.f, 1.f, "%.3f", 1.5f);
			ImGui::SliderFloat("Max QEF Error", &options.maxError, 0.f, 10.f, "%.3f", 1.5f);
			ImGui::SliderFloat("Max Edge Size", &options.maxEdgeSize, 0.f, 10.f);
			ImGui::SliderFloat("Min Angle Cosine", &options.minAngleCosine, 0.f, 1.f);
		}

		if (ImGui::CollapsingHeader("Super Primitive Config"))
		{
			if (ImGui::Button("Cube"))
			{
				primConfig = ConfigForShape(SuperPrimitiveConfig::Cube);
			}

			ImGui::SameLine();
			if (ImGui::Button("Torus"))
			{
				primConfig = ConfigForShape(SuperPrimitiveConfig::Torus);
			}

			ImGui::SameLine();
			if (ImGui::Button("Cylinder"))
			{
				primConfig = ConfigForShape(SuperPrimitiveConfig::Cylinder);
			}

			ImGui::SameLine();
			if (ImGui::Button("Pill"))
			{
				primConfig = ConfigForShape(SuperPrimitiveConfig::Pill);
			}

			ImGui::SameLine();
			if (ImGui::Button("Corridor"))
			{
				primConfig = ConfigForShape(SuperPrimitiveConfig::Corridor);
			}

			ImGui::SliderFloat4("S", &primConfig.s.x, 0.f, 2.f);
			ImGui::SliderFloat2("R", &primConfig.r.x, 0.f, 1.f);
		}

		if (ImGui::CollapsingHeader("Viewer Options"))
		{
			ImGui::SliderFloat("Mesh Scale", &viewerOpts.meshScale, 1.f, 5.f, "%.3f", 1.2f);
			if (ImGui::RadioButton("Draw Wireframe", viewerOpts.drawWireframe))
			{
				viewerOpts.drawWireframe = !viewerOpts.drawWireframe;
			}
		}

		if (ImGui::Button("Refresh"))
		{
			viewerOpts.refreshModel = true;
		}

		ImGui::End();
	}

	// Rendering
	ImGuiIO& io = ImGui::GetIO();
	glViewport(0, 0, (int)io.DisplaySize.x, (int)io.DisplaySize.y);
	ImGui::Render();

	return io.WantCaptureMouse || io.WantCaptureKeyboard;	
}

// ----------------------------------------------------------------------------

void DrawFrame(
	GLSLProgram& program, 
	const std::vector<Mesh>& meshes, 
	const glm::vec3& pos, 
	const glm::vec3& fwd, 
	const bool drawWireframe, 
	const float meshScale)
{
	glClearColor(0.3f, 0.3f, 0.3f, 0.f);
	glClear(GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT);

	glEnable(GL_DEPTH_TEST);
	glEnable(GL_CULL_FACE);
	glCullFace(GL_BACK);

	glm::mat4 projection = glm::perspective(60.f, 16.f/9.f, 0.1f, 500.f);
	glm::mat4 modelview = glm::lookAt(pos + fwd, glm::vec3(0.f), glm::vec3(0.f, 1.f, 0.f));

	glUseProgram(program.getId());

	int offset = -1 * (meshes.size() / 2);
	for (int i = 0; i < meshes.size(); i++)
	{
		const Mesh& mesh = meshes[i];
		glm::mat4 model = glm::translate(glm::mat4(1.f), (float)offset++ * glm::vec3(meshScale / 2.f, 0.f, 0.f));
		program.setUniformInt("useUniformColour", 0);
		program.setUniform("MVP", projection * modelview * model);

		glBindVertexArray(mesh.vertexArrayObj_);
		glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
		glDrawElements(GL_TRIANGLES, mesh.numIndices_, GL_UNSIGNED_INT, (void*)0);

		if (drawWireframe)
		{
			glm::mat4 wireframe = glm::translate(model, 0.08f * -fwd);

			program.setUniform("MVP", projection * modelview * wireframe);
			program.setUniformInt("useUniformColour", 1);
			program.setUniform("colour", glm::vec4(1));
			glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
			glDrawElements(GL_TRIANGLES, mesh.numIndices_, GL_UNSIGNED_INT, (void*)0);

			glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
		}
	}

	glBindVertexArray(0);
	glUseProgram(0);
}

// ----------------------------------------------------------------------------

Mesh CreateGLMesh(MeshBuffer* buffer, const float meshScale, const MeshSimplificationOptions& options)
{
	printf("Simplify iteration: error=%f\n", options.maxError);

	MeshBuffer* simplfiedMesh = new MeshBuffer;
	simplfiedMesh->numVertices = buffer->numVertices;
	simplfiedMesh->vertices = (MeshVertex*)malloc(sizeof(MeshVertex) * buffer->numVertices);
	for (int i = 0; i < buffer->numVertices; i++)
	{
		simplfiedMesh->vertices[i].xyz[0] = buffer->vertices[i].xyz[0];
		simplfiedMesh->vertices[i].xyz[1] = buffer->vertices[i].xyz[1];
		simplfiedMesh->vertices[i].xyz[2] = buffer->vertices[i].xyz[2];
		simplfiedMesh->vertices[i].xyz[3] = 1.f;

		simplfiedMesh->vertices[i].normal[0] = buffer->vertices[i].normal[0];
		simplfiedMesh->vertices[i].normal[1] = buffer->vertices[i].normal[1];
		simplfiedMesh->vertices[i].normal[2] = buffer->vertices[i].normal[2];
		simplfiedMesh->vertices[i].normal[3] = 0.f;

		simplfiedMesh->vertices[i].colour[0] = buffer->vertices[i].colour[0];
		simplfiedMesh->vertices[i].colour[1] = buffer->vertices[i].colour[1];
		simplfiedMesh->vertices[i].colour[2] = buffer->vertices[i].colour[2];
		simplfiedMesh->vertices[i].colour[3] = buffer->vertices[i].colour[3];

		simplfiedMesh->vertices[i].xyz[0] *= meshScale;
		simplfiedMesh->vertices[i].xyz[1] *= meshScale;
		simplfiedMesh->vertices[i].xyz[2] *= meshScale;
		simplfiedMesh->vertices[i].xyz[3] = 1.f;
	}

	simplfiedMesh->numTriangles = buffer->numTriangles;
	simplfiedMesh->triangles = (MeshTriangle*)malloc(sizeof(MeshTriangle) * buffer->numTriangles);
	memcpy(&simplfiedMesh->triangles[0], &buffer->triangles[0], sizeof(buffer->triangles[0]) * buffer->numTriangles);

	vec4 offset;
	offset[0] = 0.f;
	offset[1] = 0.f;
	offset[2] = 0.f;
	offset[3] = 0.f;

	ngMeshSimplifier(simplfiedMesh, offset, options);

	Mesh mesh;
	mesh.initialise();
	mesh.uploadData(simplfiedMesh);

	delete simplfiedMesh;
	return mesh;
}

// ----------------------------------------------------------------------------

int main(int argc, char** argv)
{
	if (SDL_Init(SDL_INIT_EVERYTHING) < 0)
	{
		printf("Error: SDL_Init failed: %s\n", SDL_GetError());
		return EXIT_FAILURE;
	}

	const int SCREEN_W = 1920;
	const int SCREEN_H = 1080;

	SDL_Window* window = SDL_CreateWindow("MeshSimpl", 100, 100, SCREEN_W, SCREEN_H, SDL_WINDOW_OPENGL);
	if (!window)
	{
		printf("Error: SDL_CreateWindow failed: %s\n", SDL_GetError());
		return EXIT_FAILURE;
	}

	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

	SDL_GLContext context = SDL_GL_CreateContext(window);
	if (!context)
	{
		printf("Error: SDL_GL_CreateContext failed: %s\n", SDL_GetError());
		return EXIT_FAILURE;
	}

	SDL_GL_MakeCurrent(window, context);

	glewExperimental = true;
	if (glewInit() != GLEW_OK)
	{
		printf("Error: glewInit failed\n");
		return EXIT_FAILURE;
	}

	glViewport(0, 0, SCREEN_W, SCREEN_H);

	// getting an (suprious?) error from glew here, just ignore
	glGetError();

	printf("----------------------------------------------------------------\n");
	printf("The controls are:\n");
	printf("	- hold left mouse and drag to rotate the view\n");
	printf("	- use the mouse wheel to zoom in/out\n");
	printf("	- press F1 to render a wireframe\n");
	printf("	- press F2 to regenerate the octree using a new error threshold (and generate a new mesh)\n");
	printf("----------------------------------------------------------------\n");
	printf("\n\n");

	printf("OpenGL version: %s\n", glGetString(GL_VERSION));
	printf("OpenGL shading version: %s\n", glGetString(GL_SHADING_LANGUAGE_VERSION));

	GLSLProgram program;
	if (!program.initialise() ||
		!program.compileShader(ShaderType_Vertex, "shader.vert") ||
		!program.compileShader(ShaderType_Fragment, "shader.frag") ||
		!program.link())
	{
		printf("Error: failed to create GLSL program\n");
		return EXIT_FAILURE;
	}

	// octreeSize must be a power of two!
	const int octreeSize = 64;

	float rotateX = -60.f, rotateY = 0.f;
	float distance = 100.f;
	MeshSimplificationOptions options;
	ViewerOptions viewerOpts;
	Uint32 lastFrameTime = 0;

	viewerOpts.meshScale = 1.f;
	options.maxEdgeSize = 2.5f;

	SuperPrimitiveConfig primConfig = ConfigForShape(SuperPrimitiveConfig::Cube);
	MeshBuffer* meshBuffer = GenerateMesh(primConfig);

	auto mesh = CreateGLMesh(meshBuffer, viewerOpts.meshScale, options);
	std::vector<Mesh> meshes{mesh};

	ImGui_ImplSdl_Init(window);

	bool quit = false;
	bool guiHasFocus = false;
	while (!quit)
	{
		SDL_Event event;

		while (SDL_PollEvent(&event))
		{
			if (guiHasFocus)
			{
				ImGui_ImplSdl_ProcessEvent(&event);
				continue;
			}

			switch (event.type)
			{
			case SDL_QUIT:
				quit = true;
				break;

			case SDL_MOUSEMOTION:
				HandleMouseMove(event.motion, rotateX, rotateY);
				break;

			case SDL_MOUSEWHEEL:
				HandleMouseWheel(event.wheel, distance);
				break;

			case SDL_KEYUP:
				HandleKeyPress(event.key, viewerOpts.drawWireframe);
				break;

			case SDL_KEYDOWN:
				break;
			}
		}

		if ((SDL_GetTicks() - lastFrameTime) < 33)
		{
			continue;
		}

		lastFrameTime = SDL_GetTicks();

		// calculate the forward vector and then use that find the camera position
		glm::vec3 dir(0.f, 0.f, 1.f);
		dir = glm::rotateX(dir, rotateX);
		dir = glm::rotateY(dir, rotateY);

		glm::vec3 position = dir * distance;
		DrawFrame(program, meshes, position, -dir, viewerOpts.drawWireframe, viewerOpts.meshScale);

		guiHasFocus = GUI_DrawFrame(window, viewerOpts, options, primConfig);

		SDL_GL_SwapWindow(window);

		if (viewerOpts.refreshModel)
		{
			viewerOpts.refreshModel = false;

			free(meshBuffer->vertices);
			free(meshBuffer->triangles);
			delete meshBuffer;

			meshBuffer = GenerateMesh(primConfig);
			mesh = CreateGLMesh(meshBuffer, viewerOpts.meshScale, options);

			meshes.clear();
			meshes.push_back(mesh);
		}
	}

	for (auto& mesh: meshes)
	{
		mesh.destroy();
	}

	ImGui_ImplSdl_Shutdown();

	SDL_GL_DeleteContext(context);
	SDL_Quit();

	return EXIT_SUCCESS;
}

// ----------------------------------------------------------------------------

