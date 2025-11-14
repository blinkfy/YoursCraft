#pragma once
#include "Common.h"
#include "Chunk.h"

#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <atomic>

class World {
public:
    World();
    ~World();

    Chunk* getChunk(int chunkX,int chunkZ);
    BlockType getBlock(int x,int y,int z);
    //获取方块但不创建缺失的chunk。若chunk未加载返回AIR
    BlockType getBlockIfLoaded(int x,int y,int z);
    void setBlock(int x,int y,int z,BlockType type);

    //更新区块需要lightDir来选择与渲染一致的面
    void updateChunks(const Camera& camera,const glm::vec3& lightDir);
    void render(Shader& shader,const Camera& camera,const glm::vec3& lightDir);

    //暴露chunks以用于深度/阴影通道
    std::map<std::pair<int,int>,Chunk*>& getChunks() { return chunks;}

    //返回是否存在后台未完成任务（地形/构建/上传）
    bool hasPendingWork() const;

    //在主 GL 线程处理有限数量来自工作线程的网格上传
    void processUploads(int maxUploads=4);

    //检查指定坐标的chunk是否已加载
    bool hasChunkLoaded(int chunkX,int chunkZ) const;

    //检查初始生成地形+网格上传是否完成
    bool isInitialGenerationComplete();

    //返回待处理任务总数（terrain+build+uploads）
    int getPendingTasksCount() const;

    //在世界中运行有限的水模拟,应周期性从主循环调用
    void simulateWater(const Camera& camera);

private:
    std::map<std::pair<int,int>,Chunk*> chunks;
    int renderDistance;

    //检查 chunk 是否在视锥内（用于剔除）
    bool isChunkInFrustum(Chunk* chunk,const Camera& camera,const glm::mat4& viewProj) const;

    //----- 多线程网格生成相关 -----
    struct BuildRequest {
        Chunk* chunk;
        glm::vec3 viewDir;
        glm::vec3 lightDir;
        bool full;//是否构建完整 6 面
    };

    std::mutex buildMutex;
    std::condition_variable buildCv;
    std::queue<BuildRequest> buildQueue;

    //地形生成队列：工作线程将生成地形并产出MeshData
    std::queue<Chunk*> terrainQueue;

    std::mutex uploadMutex;
    std::queue<MeshData> uploadQueue;

    std::vector<std::thread> workers;
    std::atomic<bool> workerRunning{false};
    int maxUploadsPerFrame=8;
    //控制每帧新建chunk数量以避免CPU峰值
    int maxNewChunksPerFrame=4;
};