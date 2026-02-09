// Lunar Simulation Repo by @peterkchung

#include <GLFW/glfw3.h>

class LunaApp {

public:
    void run() {
        initWindow();
        initVulkan();
        mainLoop();
        cleanUp();
    }

private:
    GLFWwindow* window = nullptr;

    void initWindow() {
        glfwInit();
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        window = glfwCreateWindow(1280, 720, "Luna", nullptr, nullptr);
    }

    void initVulkan() {
        // TBD
    }

    void mainLoop() {
        while (!glfwWindowShouldClose(window)) {
            glfwPollEvents();
        }
    }

    void cleanUp() {
        glfwDestroyWindow(window);
        glfwTerminate();
    }

};

int main() {
    LunaApp app;
    app.run();
    return 0;

}
