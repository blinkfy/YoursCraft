#include "../include/World.h"
#include "../include/Shader.h"
#include <iostream>
#include <algorithm>

World::World() : renderDistance(10) {
    //启动工作线程
    workerRunning=true;
    unsigned int threadCount=std::max(1u,std::thread::hardware_concurrency()>1 ? std::thread::hardware_concurrency()-1 : 1);
    for(unsigned int i=0;i<threadCount;++i) {
        workers.emplace_back([this]() {
            while (workerRunning) {
                Chunk* terrainChunk=nullptr;
                BuildRequest req;
                bool haveTerrain=false;
                {
                    std::unique_lock<std::mutex> lk(buildMutex);
                    buildCv.wait(lk,[this]{ return !terrainQueue.empty() || !buildQueue.empty() || !workerRunning;});
                    if(!workerRunning) return;
                    if(!terrainQueue.empty()) { 
                        terrainChunk=terrainQueue.front();
                        terrainQueue.pop();
                        haveTerrain=true;
                    }else if(!buildQueue.empty()) {
                        req=buildQueue.front();
                        buildQueue.pop();
                    }
                }

                if(haveTerrain && terrainChunk) {
                    //在工作线程生成地形
                    terrainChunk->generateTerrain();
                    //生成后创建完整网格数据
                    MeshData data=terrainChunk->buildMeshCPU(nullptr,nullptr);//full 6 面
                    {
                        std::lock_guard<std::mutex> ul(uploadMutex);
                        uploadQueue.push(std::move(data));
                    }
                    continue;
                }

                if(!req.chunk) continue;
                //在工作线程构建网格数据（仅 CPU）
                const glm::vec3* v=req.full ? nullptr : &req.viewDir;
                const glm::vec3* l=req.full ? nullptr : &req.lightDir;
                MeshData data=req.chunk->buildMeshCPU(v,l);

                //推入上传队列
                {
                    std::lock_guard<std::mutex> ul(uploadMutex);
                    uploadQueue.push(std::move(data));
                }
            }
        });
    }
}

World::~World(){ 
    //停止工作线程
    workerRunning=false;
    buildCv.notify_all();
    for(auto &t : workers) 
        if(t.joinable()) t.join();

    //删除 chunks
    for(auto &p: chunks) 
        delete p.second;
}

Chunk* World::getChunk(int chunkX,int chunkZ){ 
    auto key=std::make_pair(chunkX,chunkZ);
    if(chunks.find(key)==chunks.end()){ 
        Chunk* c=new Chunk(chunkX,chunkZ);
        //将地形生成任务加入队列由 worker 处理
        {
            std::lock_guard<std::mutex> lk(buildMutex);
            terrainQueue.push(c);
            chunks[key]=c;
        }
        buildCv.notify_one();
        return c;
    } 
    return chunks[key];
}

BlockType World::getBlock(int x,int y,int z){ 
    int chunkX=x>=0? x/CHUNK_SIZE : (x-CHUNK_SIZE+1)/CHUNK_SIZE;
    int chunkZ=z>=0? z/CHUNK_SIZE : (z-CHUNK_SIZE+1)/CHUNK_SIZE;
    int localX=x-chunkX*CHUNK_SIZE;
    int localZ=z-chunkZ*CHUNK_SIZE;
    Chunk* c=getChunk(chunkX,chunkZ);
    return c->getBlock(localX,y,localZ);
}

//获取存在的 block，不创建缺失 chunk，未加载返回 AIR
BlockType World::getBlockIfLoaded(int x,int y,int z) {
    int chunkX=x>=0? x/CHUNK_SIZE : (x-CHUNK_SIZE+1)/CHUNK_SIZE;
    int chunkZ=z>=0? z/CHUNK_SIZE : (z-CHUNK_SIZE+1)/CHUNK_SIZE;
    int localX=x-chunkX*CHUNK_SIZE;
    int localZ=z-chunkZ*CHUNK_SIZE;
    std::lock_guard<std::mutex> lk(buildMutex);
    auto it=chunks.find({chunkX,chunkZ});
    if(it==chunks.end()) return AIR;
    Chunk* c=it->second;
    return c->getBlock(localX,y,localZ);
}

bool World::hasChunkLoaded(int chunkX,int chunkZ) const {
    std::lock_guard<std::mutex> lk(const_cast<std::mutex&>(buildMutex));
    return chunks.find({chunkX,chunkZ})!=chunks.end();
}

void World::setBlock(int x,int y,int z,BlockType type){ 
    int chunkX=x>=0? x/CHUNK_SIZE : (x-CHUNK_SIZE+1)/CHUNK_SIZE;
    int chunkZ=z>=0? z/CHUNK_SIZE : (z-CHUNK_SIZE+1)/CHUNK_SIZE;
    int localX=x-chunkX*CHUNK_SIZE;
    int localZ=z-chunkZ*CHUNK_SIZE;
    Chunk* c=getChunk(chunkX,chunkZ);
    c->setBlock(localX,y,localZ,type);

    //标记邻居（如果触及区块边界），以便重建共享面
    const int lx=localX,ly=y,lz=localZ;
    auto tryMarkNeighbor=[&](int nx,int nz){
        auto it=chunks.find({nx,nz});
        if(it!=chunks.end()) it->second->setNeedsMeshUpdate(true);
    };
    if(lx==0) tryMarkNeighbor(chunkX-1,chunkZ);
    if(lx==CHUNK_SIZE-1) tryMarkNeighbor(chunkX+1,chunkZ);
    if(lz==0) tryMarkNeighbor(chunkX,chunkZ-1);
    if(lz==CHUNK_SIZE-1) tryMarkNeighbor(chunkX,chunkZ+1);

    //在主线程立即重建此 chunk 的网格，保证编辑即时可见
    //同时重建上述标记的邻居 chunk
    //注：getChunk() 可能创建了 chunk；确保仅在 chunk 存在于 map 中时重建。
    if(c) {
        c->buildMesh(nullptr,nullptr);
        c->setPendingBuild(false);
    }
    //同步重建邻居（如果存在）
    if(lx==0) { auto it=chunks.find({chunkX-1,chunkZ});
        if(it!=chunks.end()) { 
            it->second->buildMesh(nullptr,nullptr);
            it->second->setPendingBuild(false);
        } 
    }
    if(lx==CHUNK_SIZE-1) { 
        auto it=chunks.find({chunkX+1,chunkZ});
        if(it!=chunks.end()) { 
            it->second->buildMesh(nullptr,nullptr);
            it->second->setPendingBuild(false);
        }
    }
    if(lz==0) { 
        auto it=chunks.find({chunkX,chunkZ-1});
        if(it!=chunks.end()) { 
            it->second->buildMesh(nullptr,nullptr);
            it->second->setPendingBuild(false);
        }
    }
    if(lz==CHUNK_SIZE-1) {
        auto it=chunks.find({chunkX,chunkZ+1});
        if(it!=chunks.end()) { 
            it->second->buildMesh(nullptr,nullptr);
            it->second->setPendingBuild(false);
        } 
    }

    //唤醒 worker 以防其在等待队列
    buildCv.notify_all();
}



void World::updateChunks(const Camera& camera,const glm::vec3& lightDir){ 
    int playerChunkX=(int)floor(camera.position.x/CHUNK_SIZE);
    int playerChunkZ=(int)floor(camera.position.z/CHUNK_SIZE);
    int createdThisFrame=0;
    for(int x=-renderDistance;x<=renderDistance;++x) {
        for(int z=-renderDistance;z<=renderDistance;++z) {
            if(x*x+z*z>renderDistance*renderDistance) continue;
            //限制每帧新建 chunk 数量
            auto key=std::make_pair(playerChunkX+x,playerChunkZ+z);
            {
                std::lock_guard<std::mutex> lk(buildMutex);
                if(chunks.find(key)!=chunks.end()) continue;
            }
            if(createdThisFrame<maxNewChunksPerFrame) {
                getChunk(playerChunkX+x,playerChunkZ+z);
                ++createdThisFrame;
            }
            //达到限制后，下帧继续创建
        }
    }
    
    //为需要更新且未在队列中的 chunk 提交构建请求
    std::vector<BuildRequest> toSubmit;
    glm::vec3 viewDir=camera.front;

    for(auto &p : chunks) {
        Chunk* c=p.second;
        if(c->needsMeshUpdate() && !c->isPendingBuild()) {
            c->setPendingBuild(true);
            BuildRequest req;
            req.chunk=c;
            req.viewDir=viewDir;
            req.lightDir=lightDir;
            //仅当 chunk 被修改时执行完整的六面重建，否则做视图相关的选择性重建
            req.full=c->needsMeshUpdate();
            toSubmit.push_back(req);
        }
    }

    if(!toSubmit.empty()) {
        std::lock_guard<std::mutex> lk(buildMutex);
        for(auto &r : toSubmit) buildQueue.push(r);
        buildCv.notify_all();
    }

    //驱逐远处 chunk 释放 GPU 资源
    int evictionRadius=renderDistance+5;
    std::vector<std::pair<int,int>> candidates;
    for(auto &kv : chunks) candidates.push_back(kv.first);

    for(auto &key : candidates) {
        int cx=key.first;
        int cz=key.second;
        int dx=std::abs(cx-playerChunkX);
        int dz=std::abs(cz-playerChunkZ);
        if(std::max(dx,dz)<=evictionRadius) continue;//仍然足够近

        Chunk* chunkPtr=nullptr;
        auto itChunk=chunks.find(key);
        if(itChunk==chunks.end()) continue;
        chunkPtr=itChunk->second;

        //在锁内检查队列引用
        bool referenced=false;
        {
            std::lock_guard<std::mutex> lk(buildMutex);
            //检查 terrainQueue
            std::queue<Chunk*> terrainCopy=terrainQueue;
            while (!terrainCopy.empty()) { 
                if(terrainCopy.front()==chunkPtr) { 
                    referenced=true;
                    break;
                } 
                terrainCopy.pop();
            }

            //检查 buildQueue
            if(!referenced) {
                std::queue<BuildRequest> buildCopy=buildQueue;
                while (!buildCopy.empty()) { 
                    if(buildCopy.front().chunk==chunkPtr) {
                        referenced=true;
                        break;
                    }
                    buildCopy.pop();
                }
            }

            //也检查 pendingBuild 标志
            if(!referenced && chunkPtr->isPendingBuild()) referenced=true;

            if(!referenced) {
                //需要检查 uploadQueue（需 uploadMutex）
                std::lock_guard<std::mutex> ul(uploadMutex);
                std::queue<MeshData> uploadCopy=uploadQueue;
                while (!uploadCopy.empty()) { 
                    if(uploadCopy.front().chunkX==cx && uploadCopy.front().chunkZ==cz) {
                        referenced=true;
                        break;
                    }
                    uploadCopy.pop();
                }
            }

            if(!referenced) {
                //安全删除：持锁从 map 中删除并 delete chunk（在主 GL 线程）
                delete chunkPtr;
                chunks.erase(itChunk);
                //注：Chunk::~Chunk 会删除 GL 资源——必须在主 GL 线程执行
            }
        }
    }

}

//检查 chunk 是否在视锥内（背面检测+粗略边界检查）
bool World::isChunkInFrustum(Chunk* chunk,const Camera& camera,const glm::mat4& viewProj) const {
    int cx=chunk->getChunkX();
    int cz=chunk->getChunkZ();
    
    //Chunk 中心点（世界坐标）
    glm::vec3 chunkCenter(
        cx*CHUNK_SIZE+CHUNK_SIZE*0.5f,
        CHUNK_HEIGHT*0.5f,
        cz*CHUNK_SIZE+CHUNK_SIZE*0.5f
    );
    
    //1. 快速背面检测：检查 chunk 中心是否在相机前方
    glm::vec3 toChunk=chunkCenter-camera.position;
    float distanceToChunk=glm::length(toChunk);
    
    //如果距离太近（玩家可能在 chunk 内），总是渲染
    if(distanceToChunk<CHUNK_SIZE*1.5f) {
        return true;
    }
    
    glm::vec3 dirToChunk=toChunk/distanceToChunk;
    float dotProduct=glm::dot(dirToChunk,camera.front);
    
    //如果点积小于阈值，说明 chunk 在相机后方或侧面很远
    if(dotProduct<-0.5f) {
        return false;
    }
    
    //2. 粗略视锥检查：检查包围球是否与视锥相交
    float boundingRadius=glm::length(glm::vec3(CHUNK_SIZE,CHUNK_HEIGHT,CHUNK_SIZE))*0.5f;
    glm::vec4 clipPos=viewProj*glm::vec4(chunkCenter,1.0f);
    if(clipPos.w>0.0f) {
        glm::vec3 ndc=glm::vec3(clipPos)/clipPos.w;
        float ndcRadius=boundingRadius/clipPos.w*2.0f;//简化
        if(ndc.x+ndcRadius<-1.0f || ndc.x-ndcRadius>1.0f ||
            ndc.y+ndcRadius<-1.0f || ndc.y-ndcRadius>1.0f ||
            ndc.z+ndcRadius<0.0f || ndc.z-ndcRadius>1.0f) {
            return false;
        }
    }
    
    return true;
}

void World::render(Shader& shader,const Camera& camera,const glm::vec3& lightDir){ 
    glm::vec3 viewDir=camera.front;//获取相机视线方向
    
    //处理上传队列：每帧限制上传数量
    int uploadsThisFrame=0;
    while (uploadsThisFrame<maxUploadsPerFrame) {
        MeshData data;
        {
            std::lock_guard<std::mutex> lk(uploadMutex);
            if(uploadQueue.empty()) break;
            data=std::move(uploadQueue.front());uploadQueue.pop();
        }
        auto key=std::make_pair(data.chunkX,data.chunkZ);
        auto it=chunks.find(key);
        if(it!=chunks.end()) {
            it->second->uploadMeshFromData(data);
        }
        uploadsThisFrame++;
    }

    //计算 view-projection 矩阵用于视锥剔除
    int width=WINDOW_WIDTH,height=WINDOW_HEIGHT;
    glm::mat4 view=camera.getViewMatrix();
    glm::mat4 projection=glm::perspective(glm::radians(45.0f),(float)width/(float)height,0.1f,200.0f);
    glm::mat4 viewProj=projection*view;
    
    //收集可见 chunk
    std::vector<Chunk*> visibleChunks;
    visibleChunks.reserve(chunks.size());
    for(auto &p: chunks) {
        if(!isChunkInFrustum(p.second,camera,viewProj)) continue;
        visibleChunks.push_back(p.second);
    }

    //1) 不透明通道：渲染所有可见 chunk 的不透明几何
    for(Chunk* c : visibleChunks) {
        c->renderOpaque(shader,&lightDir);
    }

    //2) 透明通道：收集所有透明面，全局排序后从远到近绘制
    std::vector<std::tuple<float,int,int,Chunk*>> transFaces;
    transFaces.reserve(1024);
    for(Chunk* c : visibleChunks)
        c->collectTransparentFaces(transFaces,&viewDir,&camera.position);

    std::sort(transFaces.begin(),transFaces.end(),[](const auto &a,const auto &b){ return std::get<0>(a)>std::get<0>(b);});

    //绘制排序后的透明面，开启混合并禁用深度写入
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    shader.setFloat("specularStrength",0.0f);
    for(auto &t : transFaces) {
        int texIndex=std::get<1>(t);
        int faceIdx=std::get<2>(t);
        Chunk* chunkPtr=std::get<3>(t);
        chunkPtr->drawTransparentFace(texIndex,faceIdx);
    }
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
}

//从工作线程处理上传到 GPU 的网格（在主 GL 线程调用）
void World::processUploads(int maxUploads) {
    int uploadsThisFrame=0;
    while (uploadsThisFrame<maxUploads) {
        MeshData data;
        {
            std::lock_guard<std::mutex> lk(uploadMutex);
            if(uploadQueue.empty()) break;
            data=std::move(uploadQueue.front());
            uploadQueue.pop();
        }
        auto key=std::make_pair(data.chunkX,data.chunkZ);
        auto it=chunks.find(key);
        if(it!=chunks.end()) {
            it->second->uploadMeshFromData(data);
        }
        uploadsThisFrame++;
    }
}

//检查后台 worker 是否有待处理任务
bool World::hasPendingWork() const {
    std::lock_guard<std::mutex> lk(const_cast<std::mutex&>(buildMutex));
    std::lock_guard<std::mutex> ul(const_cast<std::mutex&>(uploadMutex));
    return !terrainQueue.empty() || !buildQueue.empty() || !uploadQueue.empty();
}
int World::getPendingTasksCount() const {
    std::lock_guard<std::mutex> lk(const_cast<std::mutex&>(buildMutex));
    std::lock_guard<std::mutex> ul(const_cast<std::mutex&>(uploadMutex));
    return terrainQueue.size()+buildQueue.size()+uploadQueue.size();
}
//检查初始地形生成及上传是否完成
bool World::isInitialGenerationComplete() {
    std::lock_guard<std::mutex> lk(buildMutex);
    std::lock_guard<std::mutex> ul(uploadMutex);
    return terrainQueue.empty() && buildQueue.empty() && uploadQueue.empty();
}
//水模拟：在相机附近运行有限的水流更新（每秒调用一次）
void World::simulateWater(const Camera& camera) {
    static float g_waterAcc=0.0f;
    float deltaTime=1.0f/60.0f;//不直接使用，仅作触发
    g_waterAcc += 1.0f/60.0f;//调用频率约 60hz，累加，下面按秒触发
    if(g_waterAcc<1.0f) return;
    g_waterAcc=fmodf(g_waterAcc,1.0f);


    int cx=(int)floor(camera.position.x);
    int cy=(int)floor(camera.position.y);
    int cz=(int)floor(camera.position.z);
    int minX=cx-2*(int)REACH_DISTANCE;
    int maxX=cx+2*(int)REACH_DISTANCE;
    int minY=cy-2*(int)REACH_DISTANCE;
    int maxY=cy+2*(int)REACH_DISTANCE;
    int minZ=cz-2*(int)REACH_DISTANCE;
    int maxZ=cz+2*(int)REACH_DISTANCE;

    int processMinY=minY;
    if(minY>=0) {
        bool found=false;
        for(int x=minX;x<=maxX && !found;++x) {
            for(int z=minZ;z<=maxZ;++z) {
                if(getBlock(x,minY,z)==WATER) {
                    found=true;
                    break;
                }
            }
        }
        if(found) processMinY=minY-1;
    }
    if(processMinY<0) processMinY=0;

    int processMaxY=maxY;
    {
        bool foundTop=false;
        for(int x=minX;x<=maxX && !foundTop;++x) {
            for(int z=minZ;z<=maxZ;++z) {
                if(getBlock(x,maxY,z)==WATER) {
                    foundTop=true;
                    break;
                }
            }
        }
        if(foundTop) processMaxY=maxY+1;
    }

    for(int y=processMinY;y<=processMaxY;++y) {
        struct Change { int x,y,z;BlockType t;};
        std::vector<Change> pendingThisLayer;
        for(int x=minX;x<=maxX;++x) {
            for(int z=minZ;z<=maxZ;++z) {
                if(y<0) continue;
                BlockType bt=getBlock(x,y,z);
                if(bt!=WATER) continue;
                if(y-1>=0 && getBlock(x,y-1,z)==AIR) {
                    setBlock(x,y-1,z,WATER);
                    pendingThisLayer.push_back({x,y,z,AIR});
                    continue;
                }
                const int dx[4]={1,-1,0,0};
                const int dz[4]={0,0,1,-1};
                for(int i=0;i<4;++i) {
                    int nx=x+dx[i];
                    int nz=z+dz[i];
                    char wn=0;
                    if(getBlock(nx-1,y,nz)==WATER)wn++;
                    if(getBlock(nx+1,y,nz)==WATER)wn++;
                    if(getBlock(nx,y,nz-1)==WATER)wn++;
                    if(getBlock(nx,y,nz+1)==WATER)wn++;
                    if(wn<4 && getBlock(x,y-1,z)==WATER) continue;
                    if(nx<minX || nx>maxX || nz<minZ || nz>maxZ) continue;
                    if(getBlock(nx,y,nz)!=AIR) continue;
                    BlockType belowTarget=(y-1>=0) ? getBlock(nx,y-1,nz) : WATER;
                    if(belowTarget!=AIR) {
                        pendingThisLayer.push_back({nx,y,nz,WATER});
                    } else {
                        if(y-1>=0 && getBlock(nx,y-1,nz)==AIR) {
                            setBlock(nx,y-1,nz,WATER);
                        }
                    }
                }
            }
        }
        for(const auto &c : pendingThisLayer) setBlock(c.x,c.y,c.z,c.t);
    }
}
