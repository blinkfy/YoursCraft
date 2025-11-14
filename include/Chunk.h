#pragma once
#include "Common.h"

struct MeshData {
    int chunkX;
    int chunkZ;
    std::vector<float> verticesByType[NUM_BLOCK_TEXTURES];
};

class Chunk {
public:
    Chunk(int x,int z);
    ~Chunk();

    void generateTerrain();
    BlockType getBlock(int x,int y,int z) const; 
    void setBlock(int x,int y,int z,BlockType type);

    //现有的 GPU 构建
    void buildMesh(const glm::vec3* viewDir=nullptr,const glm::vec3* lightDir=nullptr);//构建网格,可选视线与光源方向

    //仅 CPU 的网格生成，返回网格数据（可在工作线程中调用）
    MeshData buildMeshCPU(const glm::vec3* viewDir=nullptr,const glm::vec3* lightDir=nullptr);

    //将网格数据上传到 GPU（必须在主线程/OpenGL 上下文中调用）
    void uploadMeshFromData(const MeshData& data);

    //仅渲染不透明几何（写入深度）
    void renderOpaque(Shader &shader,const glm::vec3* lightDir=nullptr);

    //仅渲染透明几何（必须在所有不透明几何绘制后调用）
    //'viewDir' 和 'cameraPos' 用于对透明面进行从远到近排序
    void renderTransparent(Shader &shader,const glm::vec3* viewDir=nullptr,const glm::vec3* cameraPos=nullptr,const glm::vec3* lightDir=nullptr);
    
    //便捷：先渲染不透明再渲染透明
    void render(Shader &shader,const glm::vec3* viewDir=nullptr,const glm::vec3* cameraPos=nullptr,const glm::vec3* lightDir=nullptr);

    void renderDepth(Shader& depthShader);//阴影贴图渲染

    //收集透明面（供 World 进行全局排序）。
    //每个 tuple 为 (depth,texIndex,faceIndex,chunkPtr)。depth 沿 viewDir 相对于 cameraPos 计算
    void collectTransparentFaces(std::vector<std::tuple<float,int,int,Chunk*>> &out,const glm::vec3* viewDir,const glm::vec3* cameraPos) const;

    //绘制在 VBO 中生成的单个透明面：纹理组 texIndex，面索引 faceIndex（0..faceCount-1）
    void drawTransparentFace(int texIndex,int faceIndex) const;

    //获取 chunk 坐标用于视锥剔除
    int getChunkX() const { return chunkX;}
    int getChunkZ() const { return chunkZ;}

    //网格更新/挂起标志访问器
    bool needsMeshUpdate() const { return needsUpdate;}
    void setNeedsMeshUpdate(bool v) { needsUpdate=v;}
    bool isPendingBuild() const { return pendingBuild;}
    void setPendingBuild(bool v) { pendingBuild=v;}

private:
    BlockType blocks[CHUNK_SIZE][CHUNK_HEIGHT][CHUNK_SIZE];
    int chunkX,chunkZ;
    std::vector<float> verticesByType[NUM_BLOCK_TEXTURES];
    unsigned int VAOs[NUM_BLOCK_TEXTURES];
    unsigned int VBOs[NUM_BLOCK_TEXTURES];
    bool needsUpdate;
    bool isFullMesh; //true=6面完整网格，false=优化网格
    bool pendingBuild;//是否已加入构建队列
    bool gpuLoaded;//是否已在 GPU 上创建 VAO/VBO

    //Greedy Meshing 辅助方法
    void addQuad(float x,float y,float z,int width,int height,int face,BlockType type);
    void buildGreedyMesh(const glm::vec3* viewDir,const glm::vec3* lightDir);
    bool isFaceVisible(int x,int y,int z,int face,BlockType blockType) const;
    void ensureGpuLoaded();//在 GL 线程创建 VAO/VBO
    void addFace(float x,float y,float z,int face,BlockType type);
    glm::vec3 getBlockColor(BlockType type);
};
