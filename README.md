## Project Overview
![image](https://github.com/user-attachments/assets/034764dd-568f-49d2-83b9-ac37836b218c)

This is a toy deferred PBR (Physically Based Rendering) renderer I’ve been developing using DirectX 12. While the core functionality is implemented, there are still some issues to address and additional features to add. I’ll document all details and updates once they’re completed.

## Implemented Features
#### D3D12 Command Abstraction

Encapsulated common commands like shader resource binding, pipeline state changes, and draw/compute dispatch calls.

#### Memory Management

Implemented TLSF (Two-Level Segregated Fit) allocator for efficient GPU resource allocation.

#### Render Graph System

Automated pass sorting, execution, and transient render target management for deferred rendering.

#### PBR Rendering

Precompute pre-filtered environment maps, precomputed BRDF, and spherical harmonics for environment lighting.

#### Simple Compile-Time Reflection

Basic reflection mechanics for serializing/deserializing resources.

## How to Build
1. Run build.bat to create a build folder with a Visual Studio solution.
2. Open the solution and set SceneTest as the startup project.
3. Compile and run, Compilation time may be longer due to redundant compilation (to be optimized).
