#include <iostream>
#include <vector>

#include <glm/glm/glm.hpp>
#include <glm/glm/gtc/matrix_transform.hpp>
#include <glm/glm/gtc/matrix_inverse.hpp>
#include <glm/glm/gtx/intersect.hpp>
#include <glm/glm/gtx/quaternion.hpp>

#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include "TriMesh.hpp"
#include "ObjLoader.hpp"
#include "Render.hpp"

#include "imgui/imgui.h"
#include "imgui/imguiRenderGL3.h"

using namespace glm;
using namespace std;

struct MainUi {
    vec3        pointSphericalCoordinates;  // point spherical coordinates
    float       sphereRadius;               // proximity query radius
    bool        showAABB;                   // show bounding box
    bool        showClosest;                // show closest
    int         hScroll;                    // horizontal scrolling
    bool        collapse;                   // collapse show
    float       rotationAngle;              // rotation angle
    vec3        rotationAxis;               // rotation axis

    static MainUi   create(float radius) {
        return {
            vec3(radius, 0.0f, 0.0f),   // pointSphericalCoordinates
            radius,                     // sphereRadius
            true,                       // showAABB
            true,                       // showClosest
            0,                          // hScroll
            false,                      // collapse
            0.0f,                       // rotationAngle
            vec3(0.0f, 1.0f, 0.0f),     // rotationAxis
        };
    }
};

struct MeshEntry {
    const char* uiString;
    const char* fileName;
};

MeshEntry gMeshEntries[] = {
    {"Load Monkey",      "monkey.obj"},
    {"Load Tetrahedra",  "tetra.obj" }
};

void errorCallback(int error, const char* descriptor) {
    cerr << "GLFW3 Error 0x" << std::hex << error << std::dec << " - " << descriptor << endl;
}

// glfw scrolling
int glfwscroll = 0;

void scrollCallback(GLFWwindow* win, double scrollX, double scrollY) {
    glfwscroll = -scrollY;
}

void doAllThings() {
    GLFWwindow* window = glfwCreateWindow(1024, 800, "Proximity Query", NULL, NULL);
    
    if (!window) {
        cerr << "Unable to create window";
        return;
    }

    glfwMakeContextCurrent(window);

    if (glewInit() != GLEW_OK) {
        cout << "Error: unable to init glew" << endl;
        return;
    }
    if (!GLEW_VERSION_3_2) {
        cout << "Error: opengl version not supported!" << endl;
        return;
    }

    auto mesh = loadFrom("monkey.obj");

    if (mesh == nullptr) {
        return;
    }
    else {
        cout << "Mesh Loaded!" << endl;
    }

    auto meshShader = TriMeshShader::instance();

    if (meshShader == nullptr) {
        cerr << "Error: unable to load mesh shader" << endl;
        return;
    }

    auto meshView = TriMeshView::from(mesh);
    if (meshView == nullptr) {
        cerr << "Error: unable to create meshView" << endl;
        return;
    }

    auto lineShader = LineShader::instance();

    auto lineQueueView = LineQueueView::create(8192);
    if (lineQueueView == nullptr) {
        cerr << "Error: unable to create lineQueueView" << endl;
        return;
    }

    // Init UI
    if (!imguiRenderGLInit("DroidSans.ttf"))
    {
        cerr << "Could not init GUI renderer." << endl;
        return;
    }

    // glfw scrolling
    int mscroll = 0;

    auto radius = glm::length(mesh->bbox().max() - mesh->bbox().min());
    auto mainUi = MainUi::create(radius);

    glfwSetScrollCallback(window, scrollCallback);

    while (!glfwWindowShouldClose(window)) {
        int width, height;
        glfwGetFramebufferSize(window, &width, &height);

        glClearColor(0.5f, 0.5f, 0.5f, 0.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
        glViewport(0, 0, width, height);

        // render geometry
        glEnable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        auto mv = lookAt(vec3(0.0f, 0.0f, -3.0f), vec3(0.0f, 0.0f, 0.0f), vec3(0.0f, 1.0f, 0.0f))
            * scale(mat4(1.0f), vec3(0.5f, 0.5f, 0.5f))
            * mat4(angleAxis(mainUi.rotationAngle, normalize(mainUi.rotationAxis)));

        auto proj = perspective(glm::pi<float>() / 4.0f, width / (float)height, 1.0f, 1000.0f);

        meshView->render(proj, mv, vec3(0.0f, 0.0f, -20.0f));

        auto mvp = proj * mv;

        auto step = 0.1f;



        if (mainUi.showAABB) {
            lineQueueView->queueCube(mvp, mesh->bbox(), false, vec4(1.0f, 1.0f, 0.0f, 0.0f));
        }

        auto r = mainUi.pointSphericalCoordinates.x;
        auto teta = mainUi.pointSphericalCoordinates.y;
        auto phi = mainUi.pointSphericalCoordinates.z;

        auto pt = vec3(r * cos(teta) * sin(phi), r * sin(teta) * sin(phi), r * cos(phi));

        auto queryMvp = mvp * translate(mat4(1.0f), pt);

        // box/sphere intersection
        auto intersectColor = vec4(1.0f, 1.0f, 0.0f, 0.0f);
        if (AABB::intersectSphere(mesh->bbox(), pt, mainUi.sphereRadius)) {
            intersectColor = vec4(1.0f, 0.0f, 0.0f, 0.0f);
        }

        lineQueueView->queueCirclesXYZ(queryMvp, mainUi.sphereRadius, 0.1f, intersectColor);
        lineQueueView->queueCirclesXYZ(queryMvp, 0.15f, 0.1f, intersectColor);

        // sphere/closest point
        auto closestPoint = TriMesh::closestOnMesh(mesh, pt);
        


        intersectColor = vec4(1.0f, 1.0f, 0.0f, 0.0f);
        if (glm::length(closestPoint - pt) < mainUi.sphereRadius) {
            intersectColor = vec4(1.0f, 0.0f, 0.0f, 0.0f);
        }

        lineQueueView->queueLine(mvp, pt, closestPoint, intersectColor);

        if (mainUi.showClosest) {
            auto mvpClosest = mvp * translate(mat4(1.0f), closestPoint);
            lineQueueView->queueCube(mvpClosest, AABB(vec3(-0.025f, -0.025f, -0.025f), vec3(0.025f, 0.025f, 0.025f)), true, intersectColor);
        }

        lineQueueView->flush();

        // render UI
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDisable(GL_DEPTH_TEST);

        // Mouse states
        unsigned char mousebutton = 0;
        //int mscroll = 0;
        if (glfwscroll != mscroll) {
            mscroll = glfwscroll;
        }

        double mousex; double mousey;
        glfwGetCursorPos(window, &mousex, &mousey);
        mousey = height - mousey;
        int leftButton = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT);
        int rightButton = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT);
        int middleButton = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_MIDDLE);
        int toggle = 0;
        if (leftButton == GLFW_PRESS)
            mousebutton |= IMGUI_MBUT_LEFT;

        imguiBeginFrame(mousex, mousey, mousebutton, mscroll);
        mscroll = 0;
        glfwscroll = 0;

        imguiBeginScrollArea("Proximity Query", 10, 10, width / 5, height - 20, &mainUi.hScroll);
        imguiSeparatorLine();
        imguiSeparator();

        for (size_t i = 0; i < sizeof(gMeshEntries) / sizeof(MeshEntry); ++i) {
            if (imguiButton(gMeshEntries[i].uiString)) {
                auto tmp = loadFrom(gMeshEntries[i].fileName);
                if (tmp != nullptr) {
                    mesh = tmp;
                    meshView = TriMeshView::from(mesh);
                }
            }
        }

        auto toggleCollapse = imguiCollapse("Collapse", "", mainUi.collapse);
        if (!mainUi.collapse)
        {
            imguiIndent();

            toggle = imguiCheck("Show Bounding Box", mainUi.showAABB);
            if (toggle)
                mainUi.showAABB = !mainUi.showAABB;

            toggle = imguiCheck("Show Closest Point", mainUi.showClosest);
            if (toggle)
                mainUi.showClosest = !mainUi.showClosest;

            imguiLabel("Collapsible element");
            imguiUnindent();
        }
        if (toggleCollapse)
            mainUi.collapse = !mainUi.collapse;

        imguiSeparatorLine();

        imguiSlider("sc Radius",  &mainUi.pointSphericalCoordinates.x, 0.f, radius, 0.1f);
        imguiSlider("sc Azimuth", &mainUi.pointSphericalCoordinates.y, -glm::pi<float>(), glm::pi<float>(), 0.1f);
        imguiSlider("sc Polar",   &mainUi.pointSphericalCoordinates.z, -glm::pi<float>(), glm::pi<float>(), 0.1f);

        imguiSlider("PQ Radius", &mainUi.sphereRadius, 0.f, radius, 0.1f);

        imguiSeparatorLine();
        imguiLabel("Rotation");
        imguiSeparator();

        imguiSlider("axis X", &mainUi.rotationAxis.x, -1.0f, 1.0f, 0.01f);
        imguiSlider("axis Y", &mainUi.rotationAxis.y, -1.0f, 1.0f, 0.01f);
        imguiSlider("axis Z", &mainUi.rotationAxis.z, -1.0f, 1.0f, 0.01f);

        imguiSlider("Angle",  &mainUi.rotationAngle, -glm::pi<float>(), glm::pi<float>(), 0.1f);

        imguiEndScrollArea();

        imguiEndFrame();

        imguiDrawText(30 + width / 5 * 2, height - 20, IMGUI_ALIGN_LEFT, "Free text", imguiRGBA(32, 192, 32, 192));

        imguiRenderGLDraw(width, height);

        glDisable(GL_BLEND);

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    // Clean UI
    imguiRenderGLDestroy();
}

int main(int argc, char* argv[]) {
    if (!glfwInit()) {
        exit(EXIT_FAILURE);
    }

    glfwSetErrorCallback(errorCallback);

    doAllThings();  // this guaranties that all shared_ptr resources are destroyed

    glfwTerminate();
    return 0;
}