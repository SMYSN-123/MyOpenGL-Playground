🚀 My OpenGL Playground

A modern C++ rendering engine prototype built from scratch.
Focusing on low-level graphics programming patterns, RAII resource management, and Modern OpenGL (Core Profile).

✨ Features (功能特性)

[x] Modern OpenGL Context: Based on GLFW & GLAD (Core Profile 3.3).

[x] Shader System: Hot-loadable shader class with error handling.

[x] Texture Management: RAII-based texture loading using stb_image.

[x] Math & Transformations: Integrated GLM for matrix operations (Translation, Rotation, Scaling).

[ ] 3D Camera System: (Coming Soon)

[ ] Lighting Model: (Coming Soon)

🛠️ Tech Stack (技术栈)

Language: C++17

Graphics API: OpenGL 3.3

Windowing: GLFW

Loader: GLAD

Math: GLM

Assets: stb_image

📦 Build & Run (构建指南)

This project uses CMake for cross-platform building.

# 1. Clone the repo
git clone [https://github.com/SMYSN-123/MyOpenGL-Playground.git](https://github.com/SMYSN-123/MyOpenGL-Playground.git)
cd MyOpenGL-Playground

# 2. Build
mkdir build
cd build
cmake ..
cmake --build .

# 3. Run
./MyGraphicsEngine


📸 Milestones (里程碑)

Milestone 1: Window creation & Event Loop.

Milestone 2: The first Triangle (VBO/VAO setup).

Milestone 3: Shader Class encapsulation.

Milestone 4: Texture mapping support.

Milestone 5: Matrix Transformations (Rotating Crates).

Created by [Li Mingzhi] - 2026