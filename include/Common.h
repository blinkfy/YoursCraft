#pragma once

#include <iostream>
#include <vector>
#include <string>
#include <cmath>
#include <map>
#include <fstream>

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/noise.hpp>

#define HAS_SOIL 1
#include <SOIL.h>

//==================== 常量定义 ====================
constexpr int WINDOW_WIDTH=1280;
constexpr int WINDOW_HEIGHT=720;
constexpr int CHUNK_SIZE=16;
constexpr int CHUNK_HEIGHT=64;
constexpr float BLOCK_SIZE=1.0f;
constexpr float PLAYER_SPEED=5.0f;
constexpr float MOUSE_SENSITIVITY=0.1f;
constexpr float REACH_DISTANCE=5.0f;
constexpr int TARGET_FPS=60;
constexpr float TARGET_FRAME_TIME=1.0f/TARGET_FPS;

constexpr float PLAYER_WIDTH=0.6f;
constexpr float PLAYER_HEIGHT=1.8f;
constexpr float PLAYER_EYE_HEIGHT=1.6f;
constexpr float STEP_HEIGHT=1.0f;
constexpr float STEP_DURATION=0.30f;
constexpr float STEPPING_SPEED_MULT=0.6f;

constexpr int WATER_LEVEL=25;
constexpr int BASE_HEIGHT=30;
constexpr float CONTINENT_SCALE=0.008f;
constexpr float TERRAIN_SCALE=0.03f;
constexpr float DETAIL_SCALE=0.12f;
constexpr float MOUNTAIN_SCALE=0.015f;
constexpr float EROSION_SCALE=0.05f;
constexpr int MAX_TERRAIN_HEIGHT=60;
constexpr int MIN_TERRAIN_HEIGHT=10;

constexpr float RIVER_FREQ=0.015f;
constexpr float RIVER_WIDTH=0.12f;
constexpr int RIVER_DEPTH=8;

//纹理
constexpr int NUM_BLOCK_TEXTURES=9;//GRASS_TOP,DIRT,STONE,WOOD,LEAVES,SAND,WATER,GRASS_SIDE,CLOUD
extern GLuint blockTextures[NUM_BLOCK_TEXTURES];

//全景贴图（启动时）
extern GLuint panoramaTextures[6];

//标题纹理
extern GLuint titleTexture;

//副标题纹理
extern GLuint subtitleTexture;

//球体纹理（足球）
extern GLuint sphereTexture;

//==================== 方块类型 ====================
enum BlockType {
    AIR=0,   //空气（透明，不渲染）
    GRASS,     //草方块（绿色）
    DIRT,      //泥土方块（棕色）
    STONE,     //石头方块（灰色）
    WOOD,      //木头方块（褐色）
    LEAVES,    //树叶方块（深绿色）
    SAND,      //沙子方块（浅黄色）
    WATER,     //水方块（蓝色）
    CLOUD       //云块（纯白，部分透明，用于渲染天上的云）
};

//将 BlockType 映射到纹理索引（非 AIR 类型）
int blockTypeToTexIndex(BlockType t);

//前向声明（让 Camera 类可以使用 World 类）
class World;

//==================== 地形生成辅助函数 ====================
float octavePerlin(float x,float z,int octaves,float persistence,float lacunarity,float scale);
float smoothLerp(float a,float b,float t);
float calculateTerrainHeight(float worldX,float worldZ);

//==================== 生物群系 ====================
enum BiomeType {
    BIOME_OCEAN,
    BIOME_BEACH,
    BIOME_PLAINS,
    BIOME_FOREST,
    BIOME_DESERT,
    BIOME_MOUNTAINS,
    BIOME_SNOW
};

BiomeType getBiome(float worldX,float worldZ,float height);

//==================== 摄像机类 前向声明 ====================
class Camera {
public:
    glm::vec3 position;
    glm::vec3 front;
    glm::vec3 up;
    glm::vec3 right;
    float yaw;
    float pitch;

    //台阶状态
    bool isStepping=false;
    float stepElapsed=0.0f;
    float stepStartY=0.0f;
    float stepTargetDeltaY=0.0f;
    float stepPendX=0.0f;
    float stepPendZ=0.0f;

    Camera(glm::vec3 pos=glm::vec3(0.0f,32.0f,0.0f));
    glm::mat4 getViewMatrix() const;//添加const
    glm::vec3 getFootPosition() const;
    bool checkCollision(const glm::vec3& pos,World& world) const;
    void processKeyboard(int direction,float deltaTime,World& world);
    void processMouseMovement(float xoffset,float yoffset);
    //移动模式
    enum MovementMode { GRAVITY_MODE=0,FLY_MODE=1 };
    MovementMode movementMode=GRAVITY_MODE;
    void setMovementMode(MovementMode m) { movementMode=m;}
    MovementMode getMovementMode() const { return movementMode;}
    void toggleMovementMode() { 
        movementMode=(movementMode==GRAVITY_MODE) ? FLY_MODE : GRAVITY_MODE;
    }

    //每帧物理更新（在重力模式下应用重力/浮力）
    void updatePhysics(float deltaTime,World& world);
private:
    void updateCameraVectors();
    //垂直速度（重力模式）
    float verticalVelocity=0.0f;
    //重力加速度
    float gravityAcc=-9.8f;
    //在浸没时浮力强度系数
    float buoyancyFactor=0.9f;
};

//前向声明 Shader
class Shader;

//==================== Chunk/World 前向声明 ====================
class Chunk;
class World;

//纹理加载器函数
void loadBlockTextures();
void loadPanoramaTextures();





