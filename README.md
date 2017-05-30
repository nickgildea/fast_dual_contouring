This project contains a fast implementation of Dual Contouring -- or more accurately a simplified version of the algorithm. No octree is constructed, instead a regular grid is contoured (much like the leaf node grid in the original DC algorithm) & the resulting mesh is simplified using my mesh simplifier. Additionally the demo project implements the "super primitive" density function which is a single function which can be configured to represent a wide variety of volumes.

The demo makes use of my SIMD QEF implementation both for the voxel vertex placement and for the mesh simplification vertex placement. I've included the latest code for both the mesh simplifier and the SIMD QEF directly in the project.

The demo depends on Dear ImGui, SDL2, GLM and GLEW. I don't think the particular version matters, the demo project expects these to exist in the solution directory.

A pre-compiled x64 executable is included with the SDL2 and glew DLLs.

The controls are:
	- hold left mouse and drag to rotate the view
	- use the mouse wheel to zoom in/out
	- press F1 to render a wireframe

Send any questions to nick.gildea@gmail.com or @ngildea85 on Twitter

![Example](https://github.com/nickgildea/fast_dual_contouring/blob/master/example.png)
