# Midterm Oral Report
Now we have worked out a running demo of our project.
## Project Overview
A interactive gui is used to control the rendering parameters, including camera parameter, environment map and scene model selection, light direction and intensity, sampling rate, etc.
To overcome the challenge of diversity of scene model format like .obj, .gltf, .blend and so on, we first use python to convert all models to .acg format, which is a custom format we designed to store scene model data in a unified way while supporting various features like PBR material, animation, light source and so on.
We also implemented a model loader in C++ to parse .acg files and load the model data into our rendering engine.
The main algorithm is implemented in the shader part using GLSL.
## Progress
The base functionality of our project has been completed, supporting diffuse and specular materials with correct light reflection and shadow rendering.
We have downloaded several scene models from online resources and converted them to .acg format, from simple objects like cornell box to complex huge scenes like Sponza and San-Miguel.
Since we are using hlsl, the acceleration structure construction and ray tracing process are all handled by the GPU, which greatly improves the rendering performance. Besides, we also introduced a denoiser, which effectively reduces the noise in the rendered images and improves the visual quality, enabling rendering high-quality images with very low sampling rates.
For material support, we have implemented diffuse, specular and transmissive materials, and further work like principled BSDF and multi-layer materials are also implemented.
Our renderer have already supported texture mapping, as shown in the rendered "breakfast room" scene, but further work like normal mapping and displacement mapping are still needed.
Importance sampling is partially implemented.
Our demo currently supports point light, area light and environment light map(skybox) with changable intensity and direction.
Antialiasing is already supported through random jittering of camera rays.