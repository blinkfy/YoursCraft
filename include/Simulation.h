#pragma once

#include "Common.h"
#include "World.h"
#include "Shader.h"
#include <vector>

//球体实体
struct Sphere {
    glm::vec3 pos;
    glm::vec3 vel;
    float radius;
    bool active;
    int texIndex;
    glm::vec2 texOffset=glm::vec2(0.0f);
    glm::vec3 accumDisp=glm::vec3(0.0f);
    glm::quat orientation=glm::quat(1.0f,0.0f,0.0f,0.0f);
};

namespace Simulation {
    //初始化用于球体的 GPU 网格
    void initSphereMesh(int lat=12,int lon=12,float radius=0.5f);

    //在世界中生成一个球体
    void spawnSphereAt(const glm::vec3& pos,float radius,int texIndex);

    //更新模拟（物理+碰撞）
    void updateSpheres(float deltaTime,World& world);

    //根据相机移动应用玩家推动力,每帧在 updateSpheres 之前调用
    void applyPlayerPush(const Camera& camera);

    //在常规渲染通道渲染球体
    void renderSpheres(Shader& shader,GLuint sphereTexture);

    //在阴影/深度通道渲染球体
    void renderSpheresDepth(Shader& depthShader);

    //清除所有球体
    void clearSpheres();
}
