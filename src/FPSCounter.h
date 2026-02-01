#ifndef FPS_COUNTER_H
#define FPS_COUNTER_H

#include <GLFW/glfw3.h>
#include <string>
#include <iostream>

class FPSCounter {
public:
    FPSCounter() : lastTime(0.0), nbFrames(0) {}

    // 在渲染循环里调用这个函数
    void update(GLFWwindow* window) {
        double currentTime = glfwGetTime();
        nbFrames++;

        // 每过 1 秒更新一次 (1.0 可以改成 0.5 加快刷新)
        if (currentTime - lastTime >= 1.0) {
            // 计算毫秒/帧 (ms/frame) 和 帧率 (FPS)
            double msPerFrame = 1000.0 / double(nbFrames);
            int fps = nbFrames; // 这一秒内跑了多少帧

            std::string title = "MyGraphicsEngine - [ " + std::to_string(msPerFrame) + " ms/frame ] [ " + std::to_string(fps) + " FPS ]";
            
            glfwSetWindowTitle(window, title.c_str());

            // 重置计数器
            nbFrames = 0;
            lastTime += 1.0;
        }
    }

private:
    double lastTime;
    int nbFrames;
};

#endif