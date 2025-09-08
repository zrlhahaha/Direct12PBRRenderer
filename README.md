## Project Overview
This is a toy deferred PBR (Physically Based Rendering) renderer developed with DirectX 12. The project is currently under construction and may contain rendering and performance issues.

<img width="1306" height="612" alt="image" src="https://github.com/user-attachments/assets/ce9ae5fa-ee09-46bb-a390-8a1465db287e" />


### Engine Features
1. Encapsulates common commands such as shader resource binding, pipeline state changes, and draw/compute dispatch calls.

2. Implements a TLSF allocator for efficient GPU resource allocation.

3. Supports basic reflection mechanics for serializing and deserializing resources.

4. Automates pass sorting, execution, and transient resource management.

### Rendering Features
1. Precomputes pre-integrated environment and BRDF maps, along with spherical harmonics for environment lighting.

2. Supports HDR with auto exposure and ACE tone mapping.

3. Implements bloom using a separable Gaussian filter.

4. Includes clustered shading (currently under construction).

### How to Build
Run build.bat to generate a build directory with a Visual Studio solution.
