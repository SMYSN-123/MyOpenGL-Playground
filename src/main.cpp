#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <iostream>
#include <cmath>
#include "Shader.h"
#include "Camera.h"
#include "Mesh.h"
#include "Model.h"
#include "loadTexture.h"
#include "FPSCounter.h"
#include "Skybox.h"
#include "SkyboxHelper.h"

#include <glm.hpp>
#include <gtc/matrix_transform.hpp>
#include <gtc/type_ptr.hpp>

// 1. 这里写窗口大小改变的回调函数 (framebuffer_size_callback)
unsigned int loadCubemap(std::vector<std::string> faces);
void framebuffer_size_callback(GLFWwindow* window, int width, int height);
void processInput(GLFWwindow *window);
void mouse_callback(GLFWwindow* window, double xpos, double ypos);
void scroll_callback(GLFWwindow* window, double xoffset, double yoffset);
void renderCube();
void renderScreenQuad();
std::vector<glm::vec4> getFrustumCornersWorldSpace(const glm::mat4& projview);
std::vector<glm::vec4> getFrustumCornersWorldSpace(const glm::mat4& proj, const glm::mat4& view);
std::vector<glm::mat4> getLightSpaceMatrices();
void renderSphere();

class PBRMaterial;

class PBRMaterial
{
public:
    // ---------------------------------------------------------
    // 构造函数 1：传统散装贴图模式 (传入 6 张图)
    // ---------------------------------------------------------
    PBRMaterial(loadTexture& diff, loadTexture& nor, loadTexture& disp, loadTexture& metal, loadTexture& rough, loadTexture& ao) 
        : my_diff(diff), my_nor(nor), my_disp(disp), my_metal(metal), my_rough(rough), my_ao(ao), isPacked(false) {}

    // ---------------------------------------------------------
    // 构造函数 2：现代 ORM 三合一贴图模式 (传入 4 张图)
    // ---------------------------------------------------------
    // 注意：我们将 orm 贴图同时初始化给 metal, rough, ao 的引用，保证 C++ 不报错
    PBRMaterial(loadTexture& diff, loadTexture& nor, loadTexture& disp, loadTexture& orm) 
        : my_diff(diff), my_nor(nor), my_disp(disp), my_metal(orm), my_rough(orm), my_ao(orm), isPacked(true) {}

    bool isPacked; // 标记当前材质是否使用了打包贴图

    void bind(const Shader& shader) const
    {
        my_diff.bind(0);
        my_nor.bind(1);
        my_disp.bind(2);
        my_metal.bind(3); // 如果是 packed，第 3 槽位绑定的就是 ORM 贴图
        
        // 如果不是打包图，才需要绑定 4 和 5 槽位
        if (!isPacked) {
            my_rough.bind(4);
            my_ao.bind(5);
        }

        // 核心：通过 uniform 告诉 Shader 当前材质的状态
        shader.setBool("usePackedMap", isPacked); 
    }
private:
    loadTexture& my_diff;
    loadTexture& my_nor;
    loadTexture& my_disp;
    loadTexture& my_metal;
    loadTexture& my_rough;
    loadTexture& my_ao;
};

void renderScene(const Shader &shader, const PBRMaterial& pavementMat, const PBRMaterial& marbleMat, const PBRMaterial& fabricMat, const PBRMaterial& greenmatelMat);

unsigned int SCR_WIDTH = 800;
unsigned int SCR_HEIGHT = 600;

// MSAA FBO: 用于渲染 3D 场景 (抗锯齿)
unsigned int msaaFBO;
unsigned int textureColorBufferMultiSampled[2]; // MSAA 纹理数组 (0:场景, 1:亮部)
unsigned int rboMultiSampled;                // MSAA 深度缓冲

// Intermediate FBO: 用于接收 MSAA 还原后的普通图像 (做后处理)
unsigned int intermediateFBO;
unsigned int screenTexture[2];                  // 普通纹理数组 (0:场景, 1:亮部)

unsigned int pingpongFBO[2];
unsigned int pingpongTexture[2];

unsigned int floorVAO;

unsigned int sphereVAO;      //渲染球体
unsigned int indexCount;

unsigned int normalMapVAO;
unsigned int normalMapVBO;

Camera camera(glm::vec3(0.0f, 0.0f, 3.0f));

float deltaTime = 0.0f;
float lastFrame = 0.0f;

bool firstMouse = true;
float lastY = SCR_HEIGHT / 2.0f;
float lastX = SCR_WIDTH / 2.0f;

bool isMouseCaptured  = true;
bool isFullscreen = false;

int windowedPosX;
int windowedPosY;
int windowedWidth;
int windowedHeight;

bool blinn = false;
bool blinnKeyPressed = false;

bool showDebugDepth = false;
bool showDebugDepthKeyPressed = false;

bool bloom = true;
bool bloomKeyPressed = false;

const unsigned int SHADOW_WIDTH = 4096;
const unsigned int SHADOW_HEIGHT = 4096;

const float cameraNearPlane = 0.1f;
const float cameraFarPlane = 500.0f;

std::vector<float> shadowCascadeLevels{cameraFarPlane / 25.0f, cameraFarPlane / 10.0f, cameraFarPlane / 2.0f, cameraFarPlane / 1.0f};

const glm::vec3 lightColor = glm::vec3(1.0f, 1.0f, 1.0f);

const glm::vec3 lightDir = glm::normalize(glm::vec3(20.0f, 50.0f, 20.0f));

int main() {

    glfwInit();

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    glfwWindowHint(GLFW_SAMPLES, 4);

    GLFWwindow* window = glfwCreateWindow(SCR_WIDTH, SCR_HEIGHT, "MyEngine", NULL, NULL);

    if(window == NULL)
    {
        std::cout<<"Failed to create GLFW window"<<"\n";
        glfwTerminate();
        return -1;
    }

    glfwMakeContextCurrent(window);
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
    glfwSetCursorPosCallback(window, mouse_callback);
    glfwSetScrollCallback(window, scroll_callback);

    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

    if(!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
    {
        std::cout<<"Failed to initialize GLAD"<<"\n";
        return -1;
    }

    FPSCounter fpsCounter;

    Shader ourShader("../src/shader.vs", "../src/shader.fs");
    Shader screenShader("../src/screen.vs", "../src/screen.fs");
    Shader shiner("../src/shiner.vs", "../src/shiner.fs");
    Shader gaussianBlurShader("../src/gaussian_blur.vs", "../src/gaussian_blur.fs");
    Shader csmShadowDepthShader("../src/csm_shadows_depth.vs", "../src/csm_shadows_depth.fs", "../src/csm_shadows_depth.gs");
    Shader purePBRShader("../src/pure_pbr.vs", "../src/pure_pbr.fs");
    Shader equirectangularToCubemapShader("../src/equirectangular_to_cubemap.vs", "../src/equirectangular_to_cubemap.fs");
    Shader backgroundShader("../src/background.vs", "../src/background.fs");
    Shader irradianceConvolutionShader("../src/irradiance_convolution.vs", "../src/irradiance_convolution.fs");
    Shader prefilterShader("../src/prefilter.vs", "../src/prefilter.fs");
    Shader brdfShader("../src/brdf.vs", "../src/brdf.fs");
    Shader debugShader("../src/screen.vs", "../src/debug.fs");
    Shader glassballShader("../src/glass_ball.vs", "../src/glass_ball.fs");

    // pavement
    loadTexture pavement_diff("../extern/Pavement/pavement_02_diff_2k.jpg", true);
    loadTexture pavement_nor_gl("../extern/Pavement/pavement_02_nor_gl_2k.png", false);
    loadTexture pavement_disp("../extern/Pavement/pavement_02_disp_2k.png", false);
    loadTexture pavement_matel("../extern/Pavement/pavement_02_arm_2k.png", false);

    // marble
    loadTexture marble_diff("../extern/marble/marble_01_diff_2k.jpg", true);
    loadTexture marble_nor_gl("../extern/marble/marble_01_nor_gl_2k.png", false);
    loadTexture marble_disp("../extern/marble/marble_01_disp_2k.png", false);
    loadTexture marble_matel("../extern/marble/marble_01_arm_2k.png", false);

    // Patterned fabric
    loadTexture fabric_diff("../extern/Patterned fabric/floral_jacquard_diff_2k.jpg", true);
    loadTexture fabric_nor_gl("../extern/Patterned fabric/floral_jacquard_nor_gl_2k.png", false);
    loadTexture fabric_disp("../extern/Patterned fabric/floral_jacquard_disp_2k.png", false);
    loadTexture fabric_matel("../extern/Patterned fabric/floral_jacquard_arm_2k.png", false);

    // Green Metal Rust
    loadTexture matel_diff("../extern/Green Metal Rust/green_metal_rust_diff_2k.jpg", true);
    loadTexture matel_nor_gl("../extern/Green Metal Rust/green_metal_rust_nor_gl_2k.png", false);
    loadTexture matel_disp("../extern/Green Metal Rust/green_metal_rust_disp_2k.png", false);
    loadTexture matel_matel("../extern/Green Metal Rust/green_metal_rust_arm_2k.png", false);

    PBRMaterial pavementMat = {pavement_diff, pavement_nor_gl, pavement_disp, pavement_matel};
    PBRMaterial marbleMat = {marble_diff, marble_nor_gl, marble_disp, marble_matel};
    PBRMaterial fabricMat = {fabric_diff, fabric_nor_gl, fabric_disp, fabric_matel};
    PBRMaterial greenmatelMat = {matel_diff, matel_nor_gl, matel_disp, matel_matel};

    glViewport(0, 0, SCR_WIDTH, SCR_HEIGHT);

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);

    glEnable(GL_MULTISAMPLE);

    glDisable(GL_CULL_FACE);
    // glEnable(GL_CULL_FACE);
    // glCullFace(GL_BACK);
    // glFrontFace(GL_CCW);

    float floorVertices[] = {
    // Pos                  // Normal           // Tex          // Tangent (需要重新算，暂时沿用)
    // --- 三角形 1 (逆时针修正) ---
    // 原来是 V1, V2, V3 (顺时针) -> 改为 V1, V3, V2 (逆时针)
     100.0f, -2.0f,  100.0f,  0.0f, 1.0f, 0.0f,  100.0f,   0.0f,   1.0f, 0.0f, 0.0f,   0.0f, 0.0f, 1.0f, // 右下
    -100.0f, -2.0f, -100.0f,  0.0f, 1.0f, 0.0f,    0.0f, 100.0f,   1.0f, 0.0f, 0.0f,   0.0f, 0.0f, 1.0f, // 左上
    -100.0f, -2.0f,  100.0f,  0.0f, 1.0f, 0.0f,    0.0f,   0.0f,   1.0f, 0.0f, 0.0f,   0.0f, 0.0f, 1.0f, // 左下

    // --- 三角形 2 (逆时针修正) ---
    // 原来是 V4, V5, V6 (顺时针) -> 改为 V4, V6, V5 (逆时针)
     100.0f, -2.0f,  100.0f,  0.0f, 1.0f, 0.0f,  100.0f,   0.0f,   1.0f, 0.0f, 0.0f,   0.0f, 0.0f, 1.0f, // 右下
     100.0f, -2.0f, -100.0f,  0.0f, 1.0f, 0.0f,  100.0f, 100.0f,   1.0f, 0.0f, 0.0f,   0.0f, 0.0f, 1.0f, // 右上
    -100.0f, -2.0f, -100.0f,  0.0f, 1.0f, 0.0f,    0.0f, 100.0f,   1.0f, 0.0f, 0.0f,   0.0f, 0.0f, 1.0f  // 左上
};

    unsigned int uniformBlockIndexourShader = glGetUniformBlockIndex(ourShader.ProgramID, "Matrices");
    unsigned int uniformBlockIndexshinerShader = glGetUniformBlockIndex(shiner.ProgramID, "Matrices");
    unsigned int uniformBlockIndexpurePBRShader_matrices = glGetUniformBlockIndex(purePBRShader.ProgramID, "Matrices");
    unsigned int uniformBlockIndexbackgroundShader = glGetUniformBlockIndex(backgroundShader.ProgramID, "Matrices");
    unsigned int uniformBlockIndexglassballShader = glGetUniformBlockIndex(glassballShader.ProgramID, "Matrices");

    unsigned int uniformBlockIndexcsmShadowDepthShader = glGetUniformBlockIndex(csmShadowDepthShader.ProgramID, "LightSpaceMatrices");
    unsigned int uniformBlockIndexpurePBRShader_light = glGetUniformBlockIndex(purePBRShader.ProgramID, "LightSpaceMatrices");

    glUniformBlockBinding(ourShader.ProgramID, uniformBlockIndexourShader, 0);
    glUniformBlockBinding(shiner.ProgramID, uniformBlockIndexshinerShader, 0);
    glUniformBlockBinding(purePBRShader.ProgramID, uniformBlockIndexpurePBRShader_matrices, 0);
    glUniformBlockBinding(backgroundShader.ProgramID, uniformBlockIndexbackgroundShader, 0);
    glUniformBlockBinding(glassballShader.ProgramID, uniformBlockIndexglassballShader, 0);

    glUniformBlockBinding(csmShadowDepthShader.ProgramID, uniformBlockIndexcsmShadowDepthShader, 1);
    glUniformBlockBinding(purePBRShader.ProgramID, uniformBlockIndexpurePBRShader_light, 1);

    // MSAA Framebuffer
    glGenFramebuffers(1, &msaaFBO);
    glBindFramebuffer(GL_FRAMEBUFFER, msaaFBO);

    // texture
    glGenTextures(2, textureColorBufferMultiSampled);
    for(unsigned int i = 0; i < 2; i++)
    {
        glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, textureColorBufferMultiSampled[i]);
        glTexImage2DMultisample(GL_TEXTURE_2D_MULTISAMPLE, 4, GL_RGB16F, SCR_WIDTH, SCR_HEIGHT, GL_TRUE);
        glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, 0);    

        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + i, GL_TEXTURE_2D_MULTISAMPLE, textureColorBufferMultiSampled[i], 0);
    }

    // rbo
    glGenRenderbuffers(1, &rboMultiSampled);
    glBindRenderbuffer(GL_RENDERBUFFER, rboMultiSampled);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, 4, GL_DEPTH24_STENCIL8, SCR_WIDTH, SCR_HEIGHT);
    glBindRenderbuffer(GL_RENDERBUFFER, 0);

    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, rboMultiSampled);

    unsigned int attachments[2] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1 };
    glDrawBuffers(2, attachments);

    if(glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        std::cout << "ERROR::FRAMEBUFFER:: MSAA Framebuffer is not complete!" << std::endl;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // Intermediate Framebuffer
    glGenFramebuffers(1, &intermediateFBO);
    glBindFramebuffer(GL_FRAMEBUFFER, intermediateFBO);

    glGenTextures(2, screenTexture);
    for(unsigned int i = 0; i < 2; i++)
    {
        glBindTexture(GL_TEXTURE_2D, screenTexture[i]);

        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, SCR_WIDTH, SCR_HEIGHT, 0, GL_RGB, GL_UNSIGNED_BYTE, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + i, GL_TEXTURE_2D, screenTexture[i], 0);
    }

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        std::cout << "ERROR::FRAMEBUFFER:: Intermediate Framebuffer is not complete!" << std::endl;

    // ubo
    unsigned int uboMatrices;
    glGenBuffers(1, &uboMatrices);
    glBindBuffer(GL_UNIFORM_BUFFER, uboMatrices);
    glBufferData(GL_UNIFORM_BUFFER, 2 * sizeof(glm::mat4), NULL, GL_DYNAMIC_DRAW);
    glBindBuffer(GL_UNIFORM_BUFFER, 0);

    glBindBufferRange(GL_UNIFORM_BUFFER, 0, uboMatrices, 0, 2 * sizeof(glm::mat4));

    //light ubo
    unsigned int lightUboMatrices;
    glGenBuffers(1, &lightUboMatrices);
    glBindBuffer(GL_UNIFORM_BUFFER, lightUboMatrices);
    glBufferData(GL_UNIFORM_BUFFER, 16 * sizeof(glm::mat4), nullptr, GL_DYNAMIC_DRAW);
    glBindBuffer(GL_UNIFORM_BUFFER, 0);

    glBindBufferRange(GL_UNIFORM_BUFFER, 1, lightUboMatrices, 0, 16 * sizeof(glm::mat4));

    // light FBO
    unsigned int lightFBO;
    glGenFramebuffers(1, &lightFBO);

    unsigned int lightDepthMaps;
    glGenTextures(1, &lightDepthMaps);

    glBindTexture(GL_TEXTURE_2D_ARRAY, lightDepthMaps);
    glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_DEPTH_COMPONENT32F, SHADOW_WIDTH, SHADOW_HEIGHT, static_cast<int>(shadowCascadeLevels.size()) + 1, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);

    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);

    constexpr float bordercolor[] = { 1.0f, 1.0f, 1.0f, 1.0f };
    glTexParameterfv(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_BORDER_COLOR, bordercolor);

    glBindFramebuffer(GL_FRAMEBUFFER, lightFBO);
    glFramebufferTexture(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, lightDepthMaps, 0);
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);

    int status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE)
    {
        std::cout << "ERROR::FRAMEBUFFER:: Framebuffer is not complete!";
        throw 0;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // pingpong render
    glGenFramebuffers(2, pingpongFBO);
    glGenTextures(2, pingpongTexture);
    for(unsigned int i = 0; i < 2; i++)
    {
        glBindFramebuffer(GL_FRAMEBUFFER, pingpongFBO[i]);
        glBindTexture(GL_TEXTURE_2D, pingpongTexture[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, SCR_WIDTH, SCR_HEIGHT, 0, GL_RGB, GL_FLOAT, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, pingpongTexture[i], 0);
    }

    // hdr Texture
    stbi_set_flip_vertically_on_load(true);
    int width, height, nrComponents;
    float *data = stbi_loadf("../src/church_meeting_room_4k.hdr", &width, &height, &nrComponents, 0);
    unsigned int hdrTexture;
    if (data)
    {
        glGenTextures(1, &hdrTexture);
        glBindTexture(GL_TEXTURE_2D, hdrTexture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, width, height, 0, GL_RGB, GL_FLOAT, data); 

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        stbi_image_free(data);
    }
    else
    {
        std::cout << "Failed to load HDR image." << std::endl;
    }

    // capture FBO
    unsigned int captureFBO, captureRBO;
    glGenFramebuffers(1, &captureFBO);
    glGenRenderbuffers(1, &captureRBO);

    glBindFramebuffer(GL_FRAMEBUFFER, captureFBO);
    glBindRenderbuffer(GL_RENDERBUFFER, captureRBO);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, 512, 512);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, captureRBO);

    unsigned int envCubemap;
    glGenTextures(1, &envCubemap);
    glBindTexture(GL_TEXTURE_CUBE_MAP, envCubemap);
    for(unsigned int i = 0; i < 6; i++)
    {
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_RGB16F, 512, 512, 0, GL_RGB, GL_FLOAT, nullptr);
    }
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glm::mat4 captureProjection = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 10.0f);
    glm::mat4 captureViews[] = 
    {
        glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3( 1.0f,  0.0f,  0.0f), glm::vec3(0.0f, -1.0f,  0.0f)),
        glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(-1.0f,  0.0f,  0.0f), glm::vec3(0.0f, -1.0f,  0.0f)),
        glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3( 0.0f,  1.0f,  0.0f), glm::vec3(0.0f,  0.0f,  1.0f)),
        glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3( 0.0f, -1.0f,  0.0f), glm::vec3(0.0f,  0.0f, -1.0f)),
        glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3( 0.0f,  0.0f,  1.0f), glm::vec3(0.0f, -1.0f,  0.0f)),
        glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3( 0.0f,  0.0f, -1.0f), glm::vec3(0.0f, -1.0f,  0.0f))
    };

    equirectangularToCubemapShader.use();

    equirectangularToCubemapShader.setInt("equirectangularMap", 0);
    equirectangularToCubemapShader.setMat4("projection", captureProjection);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, hdrTexture);

    glViewport(0, 0, 512, 512);

    glBindFramebuffer(GL_FRAMEBUFFER, captureFBO);

    for(unsigned int i = 0; i < 6; i++)
    {
        equirectangularToCubemapShader.setMat4("view", captureViews[i]);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, envCubemap, 0);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        renderCube();
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    glBindTexture(GL_TEXTURE_CUBE_MAP, envCubemap);
    glGenerateMipmap(GL_TEXTURE_CUBE_MAP);

    unsigned int irradianceMap;
    glGenTextures(1, &irradianceMap);
    glBindTexture(GL_TEXTURE_CUBE_MAP, irradianceMap);
    for (unsigned int i = 0; i < 6; ++i)
    {
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_RGB16F, 32, 32, 0, GL_RGB, GL_FLOAT, nullptr);
    }
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glBindFramebuffer(GL_FRAMEBUFFER, captureFBO);
    glBindRenderbuffer(GL_RENDERBUFFER, captureRBO);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, 32, 32);

    irradianceConvolutionShader.use();
    
    irradianceConvolutionShader.setInt("environmentMap", 0);
    irradianceConvolutionShader.setMat4("projection", captureProjection);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_CUBE_MAP, envCubemap);

    glViewport(0, 0, 32, 32);

    glBindFramebuffer(GL_FRAMEBUFFER, captureFBO);
    for (unsigned int i = 0; i < 6; ++i)
    {
        irradianceConvolutionShader.setMat4("view", captureViews[i]);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, irradianceMap, 0);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        renderCube();
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    unsigned int prefilterMap;
    glGenTextures(1, &prefilterMap);
    glBindTexture(GL_TEXTURE_CUBE_MAP, prefilterMap);
    for (unsigned int i = 0; i < 6; ++i)
    {
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_RGB16F, 128, 128, 0, GL_RGB, GL_FLOAT, nullptr);
    }
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR); 
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glGenerateMipmap(GL_TEXTURE_CUBE_MAP);

    prefilterShader.use();

    prefilterShader.setInt("environmentMap", 0);
    prefilterShader.setMat4("projection", captureProjection);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_CUBE_MAP, envCubemap);

    glBindFramebuffer(GL_FRAMEBUFFER, captureFBO);
    unsigned int maxMiplevels = 5;
    for(unsigned int mip = 0; mip < maxMiplevels; mip++)
    {
        unsigned int mipWidth = static_cast<unsigned int>(128 * std::pow(0.5, mip));
        unsigned int mipHeight = static_cast<unsigned int>(128 * std::pow(0.5, mip));
        glBindRenderbuffer(GL_RENDERBUFFER, captureRBO);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, mipWidth, mipHeight);
        glViewport(0, 0, mipWidth, mipHeight);

        float roughness = static_cast<float>(mip) / static_cast<float>(maxMiplevels - 1);
        prefilterShader.setFloat("roughness", roughness);
        for(unsigned int i = 0; i < 6; i++)
        {
            prefilterShader.setMat4("view", captureViews[i]);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, prefilterMap, mip);

            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            renderCube();
        }
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    unsigned int brdfLUTTexture;
    glGenTextures(1, &brdfLUTTexture);

    glBindTexture(GL_TEXTURE_2D, brdfLUTTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RG16F, 512, 512, 0, GL_RG, GL_FLOAT, 0);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glBindFramebuffer(GL_FRAMEBUFFER, captureFBO);
    glBindRenderbuffer(GL_RENDERBUFFER, captureRBO);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, 512, 512);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, brdfLUTTexture, 0);

    brdfShader.use();

    glViewport(0, 0, 512, 512);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    renderScreenQuad();

    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // --- 准备用于 Debug 的 FBO ---
    unsigned int debugFBO;
    glGenFramebuffers(1, &debugFBO);
    glBindFramebuffer(GL_FRAMEBUFFER, debugFBO);

    // 将你生成的 brdfLUT 贴图挂载到这个 FBO 的颜色附件 0 上
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, brdfLUTTexture, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    debugShader.use();
    debugShader.setInt("debugTexture", 0);

    backgroundShader.use();
    backgroundShader.setInt("environmentMap", 0);

    csmShadowDepthShader.use();

    purePBRShader.use();
    purePBRShader.setInt("albedoMap", 0);
    purePBRShader.setInt("normalMap", 1);
    purePBRShader.setInt("depthMap", 2);
    purePBRShader.setInt("metallicMap", 3);
    purePBRShader.setInt("roughnessMap", 4);
    purePBRShader.setInt("aoMap", 5);
    purePBRShader.setInt("irradianceMap", 6);
    purePBRShader.setInt("prefilterMap", 7);
    purePBRShader.setInt("brdfLUT", 8);
    purePBRShader.setInt("shadowMap", 10);

    glassballShader.use();
    glassballShader.setInt("irradianceMap", 0);
    glassballShader.setInt("prefilterMap", 1);
    glassballShader.setInt("brdfLUT", 2);

    screenShader.use();
    screenShader.setInt("screenTexture", 0);
    screenShader.setInt("bloomBlur", 1);

    glBindVertexArray(0);

    // --- 渲染循环 ---
    while (!glfwWindowShouldClose(window))
    {

        float currentFrame = static_cast<float>(glfwGetTime());
        deltaTime = currentFrame - lastFrame;
        lastFrame = currentFrame;

        processInput(window);

        glClearColor(0.01f, 0.01f, 0.01f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glEnable(GL_DEPTH_TEST);

        const auto lightMatrices = getLightSpaceMatrices();
        glBindBuffer(GL_UNIFORM_BUFFER, lightUboMatrices);
        for(size_t i = 0; i < lightMatrices.size(); i++)
        {
            glBufferSubData(GL_UNIFORM_BUFFER, i * sizeof(glm::mat4), sizeof(glm::mat4), lightMatrices.data());
        }
        glBindBuffer(GL_UNIFORM_BUFFER, 0);

        glViewport(0, 0, SHADOW_WIDTH, SHADOW_HEIGHT);
        glBindFramebuffer(GL_FRAMEBUFFER, lightFBO);

        glClear(GL_DEPTH_BUFFER_BIT);

        csmShadowDepthShader.use();

        // glCullFace(GL_FRONT);

        renderScene(csmShadowDepthShader, pavementMat, marbleMat, fabricMat, greenmatelMat);

        glm::mat4 modelMirror = glm::translate(glm::mat4(1.0f), glm::vec3(-2.5f, 1.0f, 0.0f));
        csmShadowDepthShader.setMat4("model", modelMirror);
        renderSphere();

        glm::mat4 modelGlass = glm::translate(glm::mat4(1.0f), glm::vec3(2.5f, 1.0f, 0.0f));
        csmShadowDepthShader.setMat4("model", modelGlass);
        renderSphere();

        // glCullFace(GL_BACK);

        glBindFramebuffer(GL_FRAMEBUFFER, msaaFBO);

        glViewport(0, 0, SCR_WIDTH, SCR_HEIGHT);
        glClearColor(0.01f, 0.01f, 0.01f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glEnable(GL_DEPTH_TEST);

        ourShader.use();

        glm::mat4 model = glm::mat4(1.0f);
        ourShader.setMat4("model", model);

        glm::mat4 projection = glm::perspective(glm::radians(camera.Zoom), (float)SCR_WIDTH/(float)SCR_HEIGHT, 0.1f, 500.0f);
        glBindBuffer(GL_UNIFORM_BUFFER, uboMatrices);
        glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(glm::mat4), glm::value_ptr(projection));

        glm::mat4 view = camera.GetViewMatrix();
        glBindBuffer(GL_UNIFORM_BUFFER, uboMatrices);
        glBufferSubData(GL_UNIFORM_BUFFER, sizeof(glm::mat4), sizeof(glm::mat4), glm::value_ptr(view));

        purePBRShader.use();

        purePBRShader.setMat3("NormalMatrix", glm::transpose(glm::inverse(glm::mat3(model))));

        purePBRShader.setVec3("viewPos", camera.Position);
        purePBRShader.setVec3("lightDir", lightDir);
        purePBRShader.setVec3("lightColor", lightColor * 5.0f);

        purePBRShader.setBool("blinn", blinn);
        purePBRShader.setFloat("far_plane", cameraFarPlane);
        purePBRShader.setFloat("height_scale", 0.1f);
        purePBRShader.setBool("useParallax", false);

        purePBRShader.setInt("cascadeCount", static_cast<int>(shadowCascadeLevels.size()));
        for (size_t i = 0; i < shadowCascadeLevels.size(); ++i)
        {
            purePBRShader.setFloat("cascadePlaneDistances[" + std::to_string(i) + "]", shadowCascadeLevels[i]);
        }

        glActiveTexture(GL_TEXTURE6);
        glBindTexture(GL_TEXTURE_CUBE_MAP, irradianceMap);
        glActiveTexture(GL_TEXTURE7);
        glBindTexture(GL_TEXTURE_CUBE_MAP, prefilterMap);
        glActiveTexture(GL_TEXTURE8);
        glBindTexture(GL_TEXTURE_2D, brdfLUTTexture);

        glActiveTexture(GL_TEXTURE10);
        glBindTexture(GL_TEXTURE_2D_ARRAY, lightDepthMaps);

        renderScene(purePBRShader, pavementMat, marbleMat, fabricMat, greenmatelMat);

        glassballShader.use();

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_CUBE_MAP, irradianceMap);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_CUBE_MAP, prefilterMap);
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, brdfLUTTexture);

        // ==========================================
        // 🔴 渲染球体 1：完美镜面球 (Mirror)
        // ==========================================
        glassballShader.setInt("u_MaterialType", 0); // 启用标准 PBR 模式
        glassballShader.setVec3("u_AlbedoVal", glm::vec3(1.0f, 1.0f, 1.0f)); // 银白色
        glassballShader.setFloat("u_MetallicVal", 1.0f);   // 纯金属！
        glassballShader.setFloat("u_RoughnessVal", 0.0f);  // 极其光滑！

        glassballShader.setVec3("camPos", camera.Position);

        // 设置模型矩阵位置，向左平移
        modelMirror = glm::translate(glm::mat4(1.0f), glm::vec3(-2.5f, 1.0f, 0.0f));
        glassballShader.setMat4("model", modelMirror);
        renderSphere();

        // ==========================================
        // 🔵 渲染球体 2：色散水晶玻璃球 (Glass)
        // ==========================================
        glassballShader.setInt("u_MaterialType", 1); // 启用高级玻璃模式
        // 玻璃本身颜色，vec3(1)是纯净玻璃，vec3(0.9, 1.0, 0.9)会有种可口可乐玻璃瓶的微绿色
        glassballShader.setVec3("u_AlbedoVal", glm::vec3(1.0f, 1.0f, 1.0f)); 
        // 玻璃是绝缘体，不需要传 Metallic 和 Roughness，因为在 Shader 中写死了
        // (如果想要磨砂玻璃，可以在 Shader 的折射部分引入 Roughness 采样高 Lod 的环境图)
        glassballShader.setFloat("u_MetallicVal", 0.0f); 
        glassballShader.setFloat("u_RoughnessVal", 0.0f);

        // 设置模型矩阵位置，向右平移
        modelGlass = glm::translate(glm::mat4(1.0f), glm::vec3(2.5f, 1.0f, 0.0f));
        glassballShader.setMat4("model", modelGlass);
        renderSphere();

        shiner.use();

        glm::mat4 sunModel = glm::mat4(1.0f);

        float sunDistance = 150.0f; 
        glm::vec3 sunPos = lightDir * sunDistance; 

        sunModel = glm::translate(sunModel, sunPos);
        sunModel = glm::scale(sunModel, glm::vec3(8.0f)); 

        shiner.setMat4("model", sunModel);
        shiner.setVec3("lightColor", lightColor * 100.0f); 

        renderSphere();

        backgroundShader.use();

        glDepthFunc(GL_LEQUAL);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_CUBE_MAP, envCubemap);

        renderCube();

        glDepthMask(GL_TRUE);
        glDepthFunc(GL_LESS);

        // 绑定读(MSAA) 和 写(中间)
        glBindFramebuffer(GL_READ_FRAMEBUFFER, msaaFBO);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, intermediateFBO);

        // --- 第一搬：搬运正常场景 ---
        glReadBuffer(GL_COLOR_ATTACHMENT0); // 从 MSAA 的 0 号读
        glDrawBuffer(GL_COLOR_ATTACHMENT0); // 往 Intermediate 的 0 号写
        glBlitFramebuffer(0, 0, SCR_WIDTH, SCR_HEIGHT, 0, 0, SCR_WIDTH, SCR_HEIGHT, GL_COLOR_BUFFER_BIT, GL_NEAREST);

        // --- 第二搬：搬运亮部区域 ---
        glReadBuffer(GL_COLOR_ATTACHMENT1); // 从 MSAA 的 1 号读
        glDrawBuffer(GL_COLOR_ATTACHMENT1); // 往 Intermediate 的 1 号写
        glBlitFramebuffer(0, 0, SCR_WIDTH, SCR_HEIGHT, 0, 0, SCR_WIDTH, SCR_HEIGHT, GL_COLOR_BUFFER_BIT, GL_NEAREST);

        // 解绑 MSAA FBO，因为我们现在要处理的是 intermediateFBO 里的纹理
        glBindFramebuffer(GL_FRAMEBUFFER, 0);

        // 高斯模糊处理
        bool horizontal = true;
        bool first_iteration = true;
        unsigned int amount = 10;
        gaussianBlurShader.use();
        for(unsigned int i = 0; i < amount; i++)
        {
            glBindFramebuffer(GL_FRAMEBUFFER, pingpongFBO[horizontal]);
            gaussianBlurShader.setBool("horizontal", horizontal);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, first_iteration ? screenTexture[1] : pingpongTexture[!horizontal]);
            renderScreenQuad();
            horizontal = !horizontal;
            if(first_iteration)
                first_iteration = false;
        }

        // 恢复默认状态
        glBindFramebuffer(GL_FRAMEBUFFER, 0);

        glDisable(GL_DEPTH_TEST);
        glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        glViewport(0, 0, SCR_WIDTH, SCR_HEIGHT);

        if (showDebugDepth) {
            // --- 🔧 调试模式：直接 Blit BRDF 贴图 ---
            glBindFramebuffer(GL_READ_FRAMEBUFFER, debugFBO);
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0); // 0 表示屏幕默认缓冲

            // 全屏覆盖 (方便看清细节，参数填你的屏幕宽高)
            glBlitFramebuffer(0, 0, 512, 512, 0, 0, SCR_WIDTH, SCR_HEIGHT, GL_COLOR_BUFFER_BIT, GL_NEAREST);

            glBindFramebuffer(GL_FRAMEBUFFER, 0); // 恢复默认状态

            debugShader.use();

            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, brdfLUTTexture); // 绑上你的 BRDF 贴图

            renderScreenQuad();
        }
        else {
            // --- 🎨 正常模式：渲染带后处理的场景 ---
            screenShader.use();
            screenShader.setFloat("offset_x", 1.0f / SCR_WIDTH);
            screenShader.setFloat("offset_y", 1.0f / SCR_HEIGHT);

            screenShader.setFloat("exposure", 1.0f);
            screenShader.setBool("bloom", bloom);

            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, screenTexture[0]); // 场景
            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, pingpongTexture[!horizontal]); // 泛光

            // 只有正常模式才需要画这个全屏四边形
            renderScreenQuad(); 
        }

        glEnable(GL_DEPTH_TEST);

        fpsCounter.update(window);

        glfwSwapBuffers(window);
        glfwPollEvents();
    }
    glDeleteRenderbuffers(1, &rboMultiSampled);
    glDeleteFramebuffers(1, &msaaFBO);
    glDeleteFramebuffers(1, &intermediateFBO);
    
    glfwTerminate();

    return 0;
}

void processInput(GLFWwindow *window)
{
    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
        glfwSetWindowShouldClose(window, true);

        static bool tabPressed = false;

        if(glfwGetKey(window, GLFW_KEY_TAB) == GLFW_PRESS)
        {
            if(!tabPressed)
            {
                isMouseCaptured = !isMouseCaptured;
                if(isMouseCaptured)
                {
                    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
                }
                else
                {
                    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
                }
                tabPressed = true;
            }
        }
        else
        {
            tabPressed = false; // 松开按键后，解锁
        }

        static bool f11Pressed = false;

        if(glfwGetKey(window, GLFW_KEY_F11) == GLFW_PRESS)
        {
            if(!f11Pressed)
            {
                isFullscreen = !isFullscreen;

                if(isFullscreen)
                {
                    // --- 切换到全屏 ---
                    glfwGetWindowPos(window, &windowedPosX, &windowedPosY);
                    glfwGetWindowSize(window, &windowedWidth, &windowedHeight);

                    GLFWmonitor* primaryMonitor = glfwGetPrimaryMonitor();

                    const GLFWvidmode* mode = glfwGetVideoMode(primaryMonitor);

                    glfwSetWindowMonitor(window, primaryMonitor, 0, 0, mode->width, mode->height, mode->refreshRate);
                }
                else
                {
                    glfwSetWindowMonitor(window, NULL, windowedPosX, windowedPosY, windowedWidth, windowedHeight, 0);
                }
                f11Pressed = true;
            }
        }
        else
        {
            f11Pressed = false;
        }

    if (isMouseCaptured)
    {
        if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)
            camera.ProcessKeyboard(FORWARD, deltaTime);
        if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
            camera.ProcessKeyboard(BACKWARD, deltaTime);
        if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
            camera.ProcessKeyboard(LEFT, deltaTime);
        if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)
            camera.ProcessKeyboard(RIGHT, deltaTime);
    }

    if (glfwGetKey(window, GLFW_KEY_B) == GLFW_PRESS && !blinnKeyPressed) 
    {
        blinn = !blinn;
        blinnKeyPressed = true;
    }
    if (glfwGetKey(window, GLFW_KEY_B) == GLFW_RELEASE) 
    {
        blinnKeyPressed = false;
    }

    if (glfwGetKey(window, GLFW_KEY_N) == GLFW_PRESS && !showDebugDepthKeyPressed) 
    {
        showDebugDepth = !showDebugDepth;
        showDebugDepthKeyPressed = true;
    }
    if (glfwGetKey(window, GLFW_KEY_N) == GLFW_RELEASE) 
    {
        showDebugDepthKeyPressed = false;
    }

    if (glfwGetKey(window, GLFW_KEY_V) == GLFW_PRESS && !bloomKeyPressed) 
    {
        bloom = !bloom;
        bloomKeyPressed = true;
    }
    if (glfwGetKey(window, GLFW_KEY_V) == GLFW_RELEASE) 
    {
        bloomKeyPressed = false;
    }
}

void framebuffer_size_callback(GLFWwindow* window, int width, int height)
{
    SCR_WIDTH = width;
    SCR_HEIGHT = height;
    // 1. 基础操作：调整 OpenGL 视口
    glViewport(0, 0, width, height);

    // 2. 进阶操作：重新分配 FBO 的纹理大小 (Resizing Texture)
    // 只有当 width 和 height 大于 0 时才执行 (防止最小化窗口时崩溃)
    if (width > 0 && height > 0)
    {
        // 1. Resize Intermediate FBO (普通纹理)
        // 2. Resize MSAA FBO (多重采样纹理 + RBO)
        for(unsigned int i = 0; i < 2; i++)
        {
            glBindTexture(GL_TEXTURE_2D, screenTexture[i]);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, NULL);

            glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, textureColorBufferMultiSampled[i]);
            glTexImage2DMultisample(GL_TEXTURE_2D_MULTISAMPLE, 4, GL_RGB16F, width, height, GL_TRUE);
        }
        glBindTexture(GL_TEXTURE_2D, 0);
        glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, 0);

        glBindRenderbuffer(GL_RENDERBUFFER, rboMultiSampled);
        glRenderbufferStorageMultisample(GL_RENDERBUFFER, 4, GL_DEPTH24_STENCIL8, width, height);
        glBindRenderbuffer(GL_RENDERBUFFER, 0);

        // 3. ✅ 新增：Resize 乒乓缓冲 (Bloom 模糊专用)
        for (unsigned int i = 0; i < 2; i++)
        {
            glBindTexture(GL_TEXTURE_2D, pingpongTexture[i]);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, width, height, 0, GL_RGB, GL_FLOAT, NULL);
        }
        glBindTexture(GL_TEXTURE_2D, 0);

        // 打印一下日志，让你知道它在工作
        std::cout << "Window Resized: All FBOs (MSAA, Screen, PingPong) Updated to " << width << "x" << height << std::endl;
    }
}

void mouse_callback(GLFWwindow* window, double xpos, double ypos)
{

    if(!isMouseCaptured) return;

    if(firstMouse)
    {
        lastY = static_cast<float>(ypos);
        lastX = static_cast<float>(xpos);
        firstMouse = false;
    }
    float yoffset = lastY - static_cast<float>(ypos);
    float xoffset = static_cast<float>(xpos) - lastX;
    lastY = static_cast<float>(ypos);
    lastX = static_cast<float>(xpos);

    camera.ProcessMouseMovement(xoffset, yoffset);
}

void scroll_callback(GLFWwindow* window, double xoffset, double yoffset)
{
    camera.ProcessMouseScroll(static_cast<float>(yoffset));
}

void renderCube()
{
    static unsigned int cubeVAO = 0;
    static unsigned int cubeVBO = 0;
    // initialize (if necessary)
    if (cubeVAO == 0)
    {
                float vertices[] = {
            // Pos                  // Normal           // Tex      // Tangent           // Bitangent
            // Back face
            -1.0f, -1.0f, -1.0f,  0.0f,  0.0f, -1.0f, 0.0f, 0.0f,  1.0f, 0.0f, 0.0f,  0.0f, 1.0f, 0.0f, // bottom-left
             1.0f,  1.0f, -1.0f,  0.0f,  0.0f, -1.0f, 1.0f, 1.0f,  1.0f, 0.0f, 0.0f,  0.0f, 1.0f, 0.0f, // top-right
             1.0f, -1.0f, -1.0f,  0.0f,  0.0f, -1.0f, 1.0f, 0.0f,  1.0f, 0.0f, 0.0f,  0.0f, 1.0f, 0.0f, // bottom-right         
             1.0f,  1.0f, -1.0f,  0.0f,  0.0f, -1.0f, 1.0f, 1.0f,  1.0f, 0.0f, 0.0f,  0.0f, 1.0f, 0.0f, // top-right
            -1.0f, -1.0f, -1.0f,  0.0f,  0.0f, -1.0f, 0.0f, 0.0f,  1.0f, 0.0f, 0.0f,  0.0f, 1.0f, 0.0f, // bottom-left
            -1.0f,  1.0f, -1.0f,  0.0f,  0.0f, -1.0f, 0.0f, 1.0f,  1.0f, 0.0f, 0.0f,  0.0f, 1.0f, 0.0f, // top-left
            // Front face
            -1.0f, -1.0f,  1.0f,  0.0f,  0.0f,  1.0f, 0.0f, 0.0f,  1.0f, 0.0f, 0.0f,  0.0f, 1.0f, 0.0f,
             1.0f, -1.0f,  1.0f,  0.0f,  0.0f,  1.0f, 1.0f, 0.0f,  1.0f, 0.0f, 0.0f,  0.0f, 1.0f, 0.0f,
             1.0f,  1.0f,  1.0f,  0.0f,  0.0f,  1.0f, 1.0f, 1.0f,  1.0f, 0.0f, 0.0f,  0.0f, 1.0f, 0.0f,
             1.0f,  1.0f,  1.0f,  0.0f,  0.0f,  1.0f, 1.0f, 1.0f,  1.0f, 0.0f, 0.0f,  0.0f, 1.0f, 0.0f,
            -1.0f,  1.0f,  1.0f,  0.0f,  0.0f,  1.0f, 0.0f, 1.0f,  1.0f, 0.0f, 0.0f,  0.0f, 1.0f, 0.0f,
            -1.0f, -1.0f,  1.0f,  0.0f,  0.0f,  1.0f, 0.0f, 0.0f,  1.0f, 0.0f, 0.0f,  0.0f, 1.0f, 0.0f,
            // Left face
            -1.0f,  1.0f,  1.0f, -1.0f,  0.0f,  0.0f, 1.0f, 0.0f,  0.0f, 0.0f, 1.0f,  0.0f, 1.0f, 0.0f,
            -1.0f,  1.0f, -1.0f, -1.0f,  0.0f,  0.0f, 1.0f, 1.0f,  0.0f, 0.0f, 1.0f,  0.0f, 1.0f, 0.0f,
            -1.0f, -1.0f, -1.0f, -1.0f,  0.0f,  0.0f, 0.0f, 1.0f,  0.0f, 0.0f, 1.0f,  0.0f, 1.0f, 0.0f,
            -1.0f, -1.0f, -1.0f, -1.0f,  0.0f,  0.0f, 0.0f, 1.0f,  0.0f, 0.0f, 1.0f,  0.0f, 1.0f, 0.0f,
            -1.0f, -1.0f,  1.0f, -1.0f,  0.0f,  0.0f, 0.0f, 0.0f,  0.0f, 0.0f, 1.0f,  0.0f, 1.0f, 0.0f,
            -1.0f,  1.0f,  1.0f, -1.0f,  0.0f,  0.0f, 1.0f, 0.0f,  0.0f, 0.0f, 1.0f,  0.0f, 1.0f, 0.0f,
            // Right face
             1.0f,  1.0f,  1.0f,  1.0f,  0.0f,  0.0f, 1.0f, 0.0f,  0.0f, 0.0f, -1.0f, 0.0f, 1.0f, 0.0f,
             1.0f, -1.0f, -1.0f,  1.0f,  0.0f,  0.0f, 0.0f, 1.0f,  0.0f, 0.0f, -1.0f, 0.0f, 1.0f, 0.0f,
             1.0f,  1.0f, -1.0f,  1.0f,  0.0f,  0.0f, 1.0f, 1.0f,  0.0f, 0.0f, -1.0f, 0.0f, 1.0f, 0.0f,
             1.0f, -1.0f, -1.0f,  1.0f,  0.0f,  0.0f, 0.0f, 1.0f,  0.0f, 0.0f, -1.0f, 0.0f, 1.0f, 0.0f,
             1.0f,  1.0f,  1.0f,  1.0f,  0.0f,  0.0f, 1.0f, 0.0f,  0.0f, 0.0f, -1.0f, 0.0f, 1.0f, 0.0f,
             1.0f, -1.0f,  1.0f,  1.0f,  0.0f,  0.0f, 0.0f, 0.0f,  0.0f, 0.0f, -1.0f, 0.0f, 1.0f, 0.0f,
            // Bottom face
            -1.0f, -1.0f, -1.0f,  0.0f, -1.0f,  0.0f, 0.0f, 1.0f,  1.0f, 0.0f, 0.0f,  0.0f, 0.0f, 1.0f,
             1.0f, -1.0f, -1.0f,  0.0f, -1.0f,  0.0f, 1.0f, 1.0f,  1.0f, 0.0f, 0.0f,  0.0f, 0.0f, 1.0f,
             1.0f, -1.0f,  1.0f,  0.0f, -1.0f,  0.0f, 1.0f, 0.0f,  1.0f, 0.0f, 0.0f,  0.0f, 0.0f, 1.0f,
             1.0f, -1.0f,  1.0f,  0.0f, -1.0f,  0.0f, 1.0f, 0.0f,  1.0f, 0.0f, 0.0f,  0.0f, 0.0f, 1.0f,
            -1.0f, -1.0f,  1.0f,  0.0f, -1.0f,  0.0f, 0.0f, 0.0f,  1.0f, 0.0f, 0.0f,  0.0f, 0.0f, 1.0f,
            -1.0f, -1.0f, -1.0f,  0.0f, -1.0f,  0.0f, 0.0f, 1.0f,  1.0f, 0.0f, 0.0f,  0.0f, 0.0f, 1.0f,
            // Top face
            -1.0f,  1.0f, -1.0f,  0.0f,  1.0f,  0.0f, 0.0f, 1.0f,  1.0f, 0.0f, 0.0f,  0.0f, 0.0f, -1.0f,
             1.0f,  1.0f , 1.0f,  0.0f,  1.0f,  0.0f, 1.0f, 0.0f,  1.0f, 0.0f, 0.0f,  0.0f, 0.0f, -1.0f,
             1.0f,  1.0f, -1.0f,  0.0f,  1.0f,  0.0f, 1.0f, 1.0f,  1.0f, 0.0f, 0.0f,  0.0f, 0.0f, -1.0f,
             1.0f,  1.0f,  1.0f,  0.0f,  1.0f,  0.0f, 1.0f, 0.0f,  1.0f, 0.0f, 0.0f,  0.0f, 0.0f, -1.0f,
            -1.0f,  1.0f, -1.0f,  0.0f,  1.0f,  0.0f, 0.0f, 1.0f,  1.0f, 0.0f, 0.0f,  0.0f, 0.0f, -1.0f,
            -1.0f,  1.0f,  1.0f,  0.0f,  1.0f,  0.0f, 0.0f, 0.0f,  1.0f, 0.0f, 0.0f,  0.0f, 0.0f, -1.0f
        };

        glGenVertexArrays(1, &cubeVAO);
        glGenBuffers(1, &cubeVBO);
        // fill buffer
        glBindBuffer(GL_ARRAY_BUFFER, cubeVBO);
        glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
        
        // link vertex attributes
        glBindVertexArray(cubeVAO);
        
        // 1. Pos
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 14 * sizeof(float), (void*)0);
        // 2. Normal
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 14 * sizeof(float), (void*)(3 * sizeof(float)));
        // 3. TexCoords
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 14 * sizeof(float), (void*)(6 * sizeof(float)));
        // 4. Tangent
        glEnableVertexAttribArray(3);
        glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, 14 * sizeof(float), (void*)(8 * sizeof(float)));
        // 5. Bitangent
        glEnableVertexAttribArray(4);
        glVertexAttribPointer(4, 3, GL_FLOAT, GL_FALSE, 14 * sizeof(float), (void*)(11 * sizeof(float)));

        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glBindVertexArray(0);
    }
    // render Cube
    glBindVertexArray(cubeVAO);
    glDrawArrays(GL_TRIANGLES, 0, 36);
    glBindVertexArray(0);
}

void renderScreenQuad()
{
    static unsigned int screenQuadVAO = 0;
    if (screenQuadVAO == 0)
    {
        // setup plane VAO
        glGenVertexArrays(1, &screenQuadVAO);
    }
    glBindVertexArray(screenQuadVAO);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
}

void renderNormalMappedQuad()
{
    if (normalMapVAO == 0)
    {
        // positions
        glm::vec3 pos1(-1.0, 1.0, 0.0);
        glm::vec3 pos2(-1.0, -1.0, 0.0);
        glm::vec3 pos3(1.0, -1.0, 0.0);
        glm::vec3 pos4(1.0, 1.0, 0.0);
        // texture coordinates
        glm::vec2 uv1(0.0, 1.0);
        glm::vec2 uv2(0.0, 0.0);
        glm::vec2 uv3(1.0, 0.0);
        glm::vec2 uv4(1.0, 1.0);
        // normal vector
        glm::vec3 nm(0.0, 0.0, 1.0);

        // calculate tangent/bitangent vectors of both triangles
        glm::vec3 tangent1, bitangent1;
        glm::vec3 tangent2, bitangent2;
        // - triangle 1
        glm::vec3 edge1 = pos2 - pos1;
        glm::vec3 edge2 = pos3 - pos1;
        glm::vec2 deltaUV1 = uv2 - uv1;
        glm::vec2 deltaUV2 = uv3 - uv1;

        GLfloat f = 1.0f / (deltaUV1.x * deltaUV2.y - deltaUV2.x * deltaUV1.y);

        tangent1.x = f * (deltaUV2.y * edge1.x - deltaUV1.y * edge2.x);
        tangent1.y = f * (deltaUV2.y * edge1.y - deltaUV1.y * edge2.y);
        tangent1.z = f * (deltaUV2.y * edge1.z - deltaUV1.y * edge2.z);
        tangent1 = glm::normalize(tangent1);

        bitangent1.x = f * (-deltaUV2.x * edge1.x + deltaUV1.x * edge2.x);
        bitangent1.y = f * (-deltaUV2.x * edge1.y + deltaUV1.x * edge2.y);
        bitangent1.z = f * (-deltaUV2.x * edge1.z + deltaUV1.x * edge2.z);
        bitangent1 = glm::normalize(bitangent1);

        // - triangle 2
        edge1 = pos3 - pos1;
        edge2 = pos4 - pos1;
        deltaUV1 = uv3 - uv1;
        deltaUV2 = uv4 - uv1;

        f = 1.0f / (deltaUV1.x * deltaUV2.y - deltaUV2.x * deltaUV1.y);

        tangent2.x = f * (deltaUV2.y * edge1.x - deltaUV1.y * edge2.x);
        tangent2.y = f * (deltaUV2.y * edge1.y - deltaUV1.y * edge2.y);
        tangent2.z = f * (deltaUV2.y * edge1.z - deltaUV1.y * edge2.z);
        tangent2 = glm::normalize(tangent2);


        bitangent2.x = f * (-deltaUV2.x * edge1.x + deltaUV1.x * edge2.x);
        bitangent2.y = f * (-deltaUV2.x * edge1.y + deltaUV1.x * edge2.y);
        bitangent2.z = f * (-deltaUV2.x * edge1.z + deltaUV1.x * edge2.z);
        bitangent2 = glm::normalize(bitangent2);


        GLfloat quadVertices[] = {
            // Positions            // normal         // TexCoords  // Tangent                          // Bitangent
            pos1.x, pos1.y, pos1.z, nm.x, nm.y, nm.z, uv1.x, uv1.y, tangent1.x, tangent1.y, tangent1.z, bitangent1.x, bitangent1.y, bitangent1.z,
            pos2.x, pos2.y, pos2.z, nm.x, nm.y, nm.z, uv2.x, uv2.y, tangent1.x, tangent1.y, tangent1.z, bitangent1.x, bitangent1.y, bitangent1.z,
            pos3.x, pos3.y, pos3.z, nm.x, nm.y, nm.z, uv3.x, uv3.y, tangent1.x, tangent1.y, tangent1.z, bitangent1.x, bitangent1.y, bitangent1.z,

            pos1.x, pos1.y, pos1.z, nm.x, nm.y, nm.z, uv1.x, uv1.y, tangent2.x, tangent2.y, tangent2.z, bitangent2.x, bitangent2.y, bitangent2.z,
            pos3.x, pos3.y, pos3.z, nm.x, nm.y, nm.z, uv3.x, uv3.y, tangent2.x, tangent2.y, tangent2.z, bitangent2.x, bitangent2.y, bitangent2.z,
            pos4.x, pos4.y, pos4.z, nm.x, nm.y, nm.z, uv4.x, uv4.y, tangent2.x, tangent2.y, tangent2.z, bitangent2.x, bitangent2.y, bitangent2.z
        };
        // Setup plane VAO
        glGenVertexArrays(1, &normalMapVAO);
        glGenBuffers(1, &normalMapVBO);
        glBindVertexArray(normalMapVAO);
        glBindBuffer(GL_ARRAY_BUFFER, normalMapVBO);
        glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), &quadVertices, GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 14 * sizeof(GLfloat), (GLvoid*)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 14 * sizeof(GLfloat), (GLvoid*)(3 * sizeof(GLfloat)));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 14 * sizeof(GLfloat), (GLvoid*)(6 * sizeof(GLfloat)));
        glEnableVertexAttribArray(3);
        glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, 14 * sizeof(GLfloat), (GLvoid*)(8 * sizeof(GLfloat)));
        glEnableVertexAttribArray(4);
        glVertexAttribPointer(4, 3, GL_FLOAT, GL_FALSE, 14 * sizeof(GLfloat), (GLvoid*)(11 * sizeof(GLfloat)));
    }
    glBindVertexArray(normalMapVAO);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
}

std::vector<glm::vec4> getFrustumCornersWorldSpace(const glm::mat4& projview)
{
    const auto inv = glm::inverse(projview);

    std::vector<glm::vec4> frustumCorners;
    for(unsigned int x = 0; x < 2; x++)
    {
        for(unsigned int y = 0; y < 2; y++)
        {
            for(unsigned int z = 0; z < 2; z++)
            {
                const glm::vec4 pt = inv * glm::vec4(2.0f * x - 1.0f, 2.0f * y - 1.0f, 2.0f * z - 1.0f, 1.0f);
                frustumCorners.push_back(pt / pt.w);
            }
        }
    }
    return frustumCorners;
}

std::vector<glm::vec4> getFrustumCornersWorldSpace(const glm::mat4& proj, const glm::mat4& view)
{
    return getFrustumCornersWorldSpace(proj * view);
}

glm::mat4 getLightSpaceMatrix(const float& nearPlane, const float& farPlane)
{
    const auto projection = glm::perspective(glm::radians(camera.Zoom), static_cast<float>(SCR_WIDTH) / static_cast<float>(SCR_HEIGHT), nearPlane, farPlane);
    const auto view = camera.GetViewMatrix();
    const auto corners = getFrustumCornersWorldSpace(projection, view);

    glm::vec3 center = glm::vec3(0.0f);
    for(const auto& v : corners)
    {
        center += glm::vec3(v.x, v.y, v.z);
    }
    center /= static_cast<float>(corners.size());

    const auto lightView = glm::lookAt(center + lightDir, center, glm::vec3(0.0f, 1.0f, 0.0f));

    float minX = std::numeric_limits<float>::max();
    float maxX = std::numeric_limits<float>::lowest();
    float minY = std::numeric_limits<float>::max();
    float maxY = std::numeric_limits<float>::lowest();
    float minZ = std::numeric_limits<float>::max();
    float maxZ = std::numeric_limits<float>::lowest();
    for(const auto& v : corners)
    {
        const auto trf = lightView * v;
        minX = std::min(minX, trf.x);
        maxX = std::max(maxX, trf.x);
        minY = std::min(minY, trf.y);
        maxY = std::max(maxY, trf.y);
        minZ = std::min(minZ, trf.z);
        maxZ = std::max(maxZ, trf.z);
    }

    // constexpr float zMult = 10.0f;
    // if(minZ < 0)
    // {
    //     minZ *= zMult;
    // }
    // else
    // {
    //     minZ /= zMult;
    // }
    // if(maxZ < 0)
    // {
    //     maxZ /= zMult;
    // }
    // else
    // {
    //     maxZ *= zMult;
    // }

    float zMargin = 100.0f;
    minZ -= zMargin; 
    maxZ += zMargin * 0.5f;

    float sideMargin  = (maxX - minX) / 4096; 

    minX = std::floor(minX / sideMargin) * sideMargin;
    maxX = std::floor(maxX / sideMargin) * sideMargin;
    minY = std::floor(minY / sideMargin) * sideMargin;
    maxY = std::floor(maxY / sideMargin) * sideMargin;

    const glm::mat4 lightProjection = glm::ortho(minX, maxX, minY, maxY, minZ, maxZ);

    return lightProjection * lightView;
}

std::vector<glm::mat4> getLightSpaceMatrices()
{
    std::vector<glm::mat4> ret;
    for(size_t i = 0; i < shadowCascadeLevels.size() + 1; i++)
    {
        if(i == 0)
        {
            ret.push_back(getLightSpaceMatrix(cameraNearPlane, shadowCascadeLevels[i]));
        }
        else if(i < shadowCascadeLevels.size())
        {
            ret.push_back(getLightSpaceMatrix(shadowCascadeLevels[i - 1], shadowCascadeLevels[i]));
        }
        else
        {
            ret.push_back(getLightSpaceMatrix(shadowCascadeLevels[i - 1], cameraFarPlane));
        }
    }
    return ret;
}

void renderSphere()
{
    if(sphereVAO == 0)
    {
        glGenVertexArrays(1, &sphereVAO);

        unsigned int sphereVBO;
        unsigned int sphereEBO;

        glGenBuffers(1, &sphereVBO);
        glGenBuffers(1, &sphereEBO);

        std::vector<glm::vec3> position;
        std::vector<glm::vec2> uv;
        std::vector<glm::vec3> normals;
        std::vector<glm::vec3> tangents;
        std::vector<glm::vec3> bitangents;
        std::vector<unsigned int> indices;

        const unsigned int X_SEGMENTS = 64;
        const unsigned int Y_SEGMENTS = 64;
        const float PI = 3.14159265359f;

        for(unsigned int x = 0; x <= X_SEGMENTS; x++)
        {
            for(unsigned int y = 0; y <= Y_SEGMENTS; y++)
            {
                float xSegment = static_cast<float>(x) / static_cast<float>(X_SEGMENTS);
                float ySegment = static_cast<float>(y) / static_cast<float>(Y_SEGMENTS);

                float yPos = std::cos(ySegment * PI);
                float sliceRadius = std::sin(ySegment * PI);

                float xPos = std::cos(xSegment * 2.0f * PI) * sliceRadius;
                float zPos = std::sin(xSegment * 2.0f * PI) * sliceRadius;

                // U方向的导数 (切线)
                glm::vec3 tangent(
                    -std::sin(xSegment * 2.0f * PI), 
                    0.0f, 
                    std::cos(xSegment * 2.0f * PI)
                );
                
                // V方向的导数 (副切线)
                glm::vec3 bitangent(
                    std::cos(xSegment * 2.0f * PI) * std::cos(ySegment * PI),
                    -std::sin(ySegment * PI),
                    std::sin(xSegment * 2.0f * PI) * std::cos(ySegment * PI)
                );

                position.push_back(glm::vec3(xPos, yPos, zPos));
                uv.push_back(glm::vec2(xSegment, ySegment));
                normals.push_back(glm::vec3(xPos, yPos, zPos));
                tangents.push_back(tangent);
                bitangents.push_back(bitangent);
            }
        }

        bool oddRow = false;
        for(unsigned int y = 0; y < Y_SEGMENTS; y++)
        {
            if(!oddRow)
            {
                for(unsigned int x = 0; x <= X_SEGMENTS; x++)
                {
                    indices.push_back(y * (X_SEGMENTS + 1) + x);
                    indices.push_back((y + 1) * (X_SEGMENTS + 1) + x);
                }
            }
            else
            {
                for(int x = X_SEGMENTS; x >= 0; x--)
                {
                    indices.push_back((y + 1) * (X_SEGMENTS + 1) + x);
                    indices.push_back(y * (X_SEGMENTS + 1) + x);
                }
            }
            oddRow = !oddRow;
        }
        indexCount = static_cast<unsigned int>(indices.size());

        std::vector<float> data;
        for(unsigned int i = 0; i < position.size(); i++)
        {
            data.push_back(position[i].x);
            data.push_back(position[i].y);
            data.push_back(position[i].z);

            if(normals.size() > 0)
            {
                data.push_back(normals[i].x);
                data.push_back(normals[i].y);
                data.push_back(normals[i].z);
            }

            if(uv.size() > 0)
            {
                data.push_back(uv[i].x);
                data.push_back(uv[i].y);
            }

            data.push_back(tangents[i].x);
            data.push_back(tangents[i].y);
            data.push_back(tangents[i].z);

            data.push_back(bitangents[i].x);
            data.push_back(bitangents[i].y);
            data.push_back(bitangents[i].z);
        }
        glBindVertexArray(sphereVAO);

        glBindBuffer(GL_ARRAY_BUFFER, sphereVBO);
        glBufferData(GL_ARRAY_BUFFER, data.size() * sizeof(float), &data[0], GL_STATIC_DRAW);

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, sphereEBO);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int), &indices[0], GL_STATIC_DRAW);

        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 14 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 14 * sizeof(float), (void*)(3 * sizeof(float)));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 14 * sizeof(float), (void*)(6 * sizeof(float)));
        glEnableVertexAttribArray(3);
        glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, 14 * sizeof(float), (void*)(8 * sizeof(float)));
        glEnableVertexAttribArray(4);
        glVertexAttribPointer(4, 3, GL_FLOAT, GL_FALSE, 14 * sizeof(float), (void*)(11 * sizeof(float)));
    }
    glBindVertexArray(sphereVAO);
    glDrawElements(GL_TRIANGLE_STRIP, indexCount, GL_UNSIGNED_INT, 0);
}

void renderScene(const Shader &shader, const PBRMaterial& pavementMat, const PBRMaterial& marbleMat, const PBRMaterial& fabricMat, const PBRMaterial& greenmatelMat)
{
    // --- 1. 渲染展台/地板 (Floor) ---
    // 给材质球一个明亮的底座，方便接住你完美的 CSM 阴影
    pavementMat.bind(shader);
    glm::mat4 model = glm::mat4(1.0f);
    model = glm::translate(model, glm::vec3(0.0f, -0.5f, 0.0f)); // 稍微下沉，让球体正好坐落在上面
    model = glm::scale(model, glm::vec3(8.0f, 0.5f, 8.0f));     // 变成一个长条形的展台
    shader.setMat4("model", model);
    shader.setMat3("NormalMatrix", glm::transpose(glm::inverse(glm::mat3(model))));
    renderCube(); 

    // --- 2. 渲染材质球 (The Sphere Gallery) ---
    marbleMat.bind(shader);
    model = glm::mat4(1.0f);
    model = glm::translate(model, glm::vec3(0.0f, 1.0f, 0.0f)); // 半径为1，Y设为1正好贴地
    shader.setMat4("model", model);
    shader.setMat3("NormalMatrix", glm::transpose(glm::inverse(glm::mat3(model))));
    renderSphere();

    fabricMat.bind(shader);
    model = glm::mat4(1.0f);
    model = glm::translate(model, glm::vec3(-5.0f, 1.0f, 0.0f));
    shader.setMat4("model", model);
    shader.setMat3("NormalMatrix", glm::transpose(glm::inverse(glm::mat3(model))));
    renderSphere();

    greenmatelMat.bind(shader);
    model = glm::mat4(1.0f);
    model = glm::translate(model, glm::vec3(5.0f, 1.0f, 0.0f));
    shader.setMat4("model", model);
    shader.setMat3("NormalMatrix", glm::transpose(glm::inverse(glm::mat3(model))));
    renderSphere();
}