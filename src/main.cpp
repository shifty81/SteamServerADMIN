#include "MainWindow.hpp"
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <GLFW/glfw3.h>
#include <cstdio>

static void glfwErrorCallback(int error, const char *description)
{
    std::fprintf(stderr, "GLFW Error %d: %s\n", error, description);
}

int main(int /*argc*/, char * /*argv*/[])
{
    glfwSetErrorCallback(glfwErrorCallback);
    if (!glfwInit())
        return 1;

    const char *glslVersion = "#version 130";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);

    GLFWwindow *glfwWindow =
        glfwCreateWindow(1600, 900, "SSA \xe2\x80\x93 Steam Server ADMIN", nullptr, nullptr);
    if (!glfwWindow) {
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(glfwWindow);
    glfwSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
#ifdef IMGUI_HAS_DOCK
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
#endif

    ImGui::StyleColorsDark();

    ImGui_ImplGlfw_InitForOpenGL(glfwWindow, true);
    ImGui_ImplOpenGL3_Init(glslVersion);

    MainWindow window(glfwWindow);

    while (!glfwWindowShouldClose(glfwWindow) && !window.wantQuit()) {
        glfwPollEvents();

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        window.render();

        ImGui::Render();
        int displayW = 0;
        int displayH = 0;
        glfwGetFramebufferSize(glfwWindow, &displayW, &displayH);
        glViewport(0, 0, displayW, displayH);
        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

#ifdef IMGUI_HAS_DOCK
        if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
            GLFWwindow *backupCtx = glfwGetCurrentContext();
            ImGui::UpdatePlatformWindows();
            ImGui::RenderPlatformWindowsDefault();
            glfwMakeContextCurrent(backupCtx);
        }
#endif

        glfwSwapBuffers(glfwWindow);
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(glfwWindow);
    glfwTerminate();
    return 0;
}
