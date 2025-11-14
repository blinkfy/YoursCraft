#include "../include/Chunk.h"
#include "../include/Common.h"
#include "../include/Shader.h"
#include "../include/World.h"

// 访问全局 world，用于跨区块边界查询邻居
extern World world;

namespace {
    //水面的平铺大小（以世界坐标单位计）
    constexpr float WATER_TILE_SIZE=2.0f;//2x2 方块共用一张贴图

    //将一个顶点（位置、uv、法线）写入缓冲
    inline void pushVertex(std::vector<float> &buf,const glm::vec3 &pos,const glm::vec2 &uv,const glm::vec3 &normal) {
        //位置 (3)
        buf.push_back(pos.x);
        buf.push_back(pos.y);
        buf.push_back(pos.z);
        //纹理坐标 (2)
        buf.push_back(uv.x);
        buf.push_back(uv.y);
        //法线 (3)
        buf.push_back(normal.x);
        buf.push_back(normal.y);
        buf.push_back(normal.z);
    }

    //计算某个面某个顶点的纹理坐标
    inline glm::vec2 computeTexCoords(BlockType type,int face,const glm::vec3 &worldPos,int vi,const float texProto[12]) {
        if(type==WATER && face==4) {
            //水顶面使用世界坐标连续 UV，使相邻 chunk 对齐
            float uf=worldPos.x/WATER_TILE_SIZE;
            float vf=worldPos.z/WATER_TILE_SIZE;
            //不取小数部分，避免贪婪四边形插值伪影；依赖 GL_REPEAT 平铺
            return glm::vec2(uf,vf);
        }
        //默认使用面局部的 texProto（不再翻转 V）
        int ti=vi*2;
        float u=texProto[ti];
        float v=texProto[ti+1];
        return glm::vec2(u,v);
    }
}

//构造函数：初始化方块数据为 AIR，并创建 VAO/VBO
Chunk::Chunk(int x,int z) : chunkX(x),chunkZ(z),needsUpdate(true),isFullMesh(false),pendingBuild(false),gpuLoaded(false) {
    //初始化方块为空气
    for(int xi=0;xi<CHUNK_SIZE;++xi)
        for(int y=0;y<CHUNK_HEIGHT;++y)
            for(int zi=0;zi<CHUNK_SIZE;++zi)
                blocks[xi][y][zi]=AIR;

    // 将 GL 资源创建延迟到上传时，避免大量创建 chunk 时卡顿
    // glGenVertexArrays(NUM_BLOCK_TEXTURES, VAOs);
    // glGenBuffers(NUM_BLOCK_TEXTURES, VBOs);
}

//析构：释放 OpenGL 资源
Chunk::~Chunk() {
    if(gpuLoaded) {
        glDeleteVertexArrays(NUM_BLOCK_TEXTURES,VAOs);
        glDeleteBuffers(NUM_BLOCK_TEXTURES,VBOs);
    }
}

// 确保 GPU 资源存在（必须在 GL 线程调用）
void Chunk::ensureGpuLoaded() {
    if(gpuLoaded) return;
    glGenVertexArrays(NUM_BLOCK_TEXTURES,VAOs);
    glGenBuffers(NUM_BLOCK_TEXTURES,VBOs);
    gpuLoaded=true;
}

//获取方块类型（越界返回 AIR）
BlockType Chunk::getBlock(int x,int y,int z) const {
    if(x<0 || x>=CHUNK_SIZE || y<0 || y>=CHUNK_HEIGHT || z<0 || z>=CHUNK_SIZE) return AIR;
    return blocks[x][y][z];
}

//设置方块并标记需要重建网格
void Chunk::setBlock(int x,int y,int z,BlockType type) {
    if(x<0 || x>=CHUNK_SIZE || y<0 || y>=CHUNK_HEIGHT || z<0 || z>=CHUNK_SIZE) return;
    blocks[x][y][z]=type;
    needsUpdate=true;
}

//根据方块类型返回基础颜色（仅用于调试/替代贴图时）
glm::vec3 Chunk::getBlockColor(BlockType type) {
    switch (type) {
        case GRASS: return glm::vec3(0.4f,0.8f,0.3f);
        case DIRT:  return glm::vec3(0.6f,0.4f,0.2f);
        case STONE: return glm::vec3(0.5f);
        case WOOD:  return glm::vec3(0.6f,0.3f,0.1f);
        case LEAVES:return glm::vec3(0.2f,0.6f,0.2f);
        case CLOUD: return glm::vec3(1.0f,1.0f,1.0f);
        case SAND:  return glm::vec3(0.9f,0.9f,0.6f);
        case WATER: return glm::vec3(0.2f,0.4f,0.8f);
        default:    return glm::vec3(1.0f);
    }
}

//生成地形：第一遍计算高度图，第二遍按高度和生物群系填充方块
void Chunk::generateTerrain() {
    //第一遍：为当前 chunk 计算高度图
    int heightMap[CHUNK_SIZE][CHUNK_SIZE];
    for(int cx=0;cx<CHUNK_SIZE;++cx) {
        for(int cz=0;cz<CHUNK_SIZE;++cz) {
            float worldX=static_cast<float>(chunkX*CHUNK_SIZE+cx);
            float worldZ=static_cast<float>(chunkZ*CHUNK_SIZE+cz);
            float h=calculateTerrainHeight(worldX,worldZ);
            int terrainHeight=static_cast<int>(h);
            //河谷雕刻
            float riverNoise=octavePerlin(worldX,worldZ,3,0.5f,2.0f,RIVER_FREQ);
            float riverBand=std::abs(riverNoise);
            if(riverBand<RIVER_WIDTH) {
                float t=1.0f-(riverBand/RIVER_WIDTH);
                int carve=static_cast<int>(std::round(t*RIVER_DEPTH));
                terrainHeight=std::max(WATER_LEVEL-3,terrainHeight-carve);
            }
            heightMap[cx][cz]=terrainHeight;
        }
    }

    //第二遍：根据高度图与生物群系垂直填充方块
    for(int cx=0;cx<CHUNK_SIZE;++cx) {
        for(int cz=0;cz<CHUNK_SIZE;++cz) {
            float worldX=static_cast<float>(chunkX*CHUNK_SIZE+cx);
            float worldZ=static_cast<float>(chunkZ*CHUNK_SIZE+cz);
            int terrainHeight=heightMap[cx][cz];

            float riverNoise=octavePerlin(worldX,worldZ,3,0.5f,2.0f,RIVER_FREQ);
            float riverBand=std::abs(riverNoise);
            bool isRiver=(riverBand<RIVER_WIDTH);

            BiomeType biome=getBiome(worldX,worldZ,terrainHeight);

            for(int y=0;y<CHUNK_HEIGHT;++y) {
                if(y<terrainHeight-5) {
                    blocks[cx][y][cz]=STONE;
                } else if(y<terrainHeight-1) {
                    blocks[cx][y][cz]=DIRT;
                } else if(y<terrainHeight) {
                    if(isRiver && terrainHeight<=WATER_LEVEL+2) {
                        blocks[cx][y][cz]=SAND;
                    } else if(biome==BIOME_BEACH || biome==BIOME_DESERT || biome==BIOME_OCEAN) {
                        blocks[cx][y][cz]=SAND;
                    } else if(biome==BIOME_SNOW || biome==BIOME_MOUNTAINS) {
                        blocks[cx][y][cz]=STONE;
                    } else {
                        blocks[cx][y][cz]=GRASS;
                    }
                } else {
                    blocks[cx][y][cz]=AIR;
                }
            }

            //若地形低于水面，则填充水直到水位
            if(terrainHeight<WATER_LEVEL) {
                for(int y=terrainHeight;y<WATER_LEVEL && y<CHUNK_HEIGHT;++y) {
                    blocks[cx][y][cz]=WATER;
                }
            }
        }
    }

    //树生成：在草地上按生物群系概率生成树干（WOOD）和树冠（LEAVES）
    for(int cx=0;cx<CHUNK_SIZE;++cx) {
        for(int cz=0;cz<CHUNK_SIZE;++cz) {
            float worldX=static_cast<float>(chunkX*CHUNK_SIZE+cx);
            float worldZ=static_cast<float>(chunkZ*CHUNK_SIZE+cz);
            int topY=heightMap[cx][cz];
            if(topY<=0 || topY>=CHUNK_HEIGHT-8) continue;//留空间给树冠
            int groundY=topY-1;
            if(groundY<0) continue;
            if(blocks[cx][groundY][cz]!=GRASS) continue;//只在草方块上

            BiomeType biome=getBiome(worldX,worldZ,topY);

            //避免在靠近 chunk 边缘处生成树（防止树冠跨 chunk 被切割）
            const int borderMargin=4;//应>=最大叶冠半径
            if(cx<borderMargin || cz<borderMargin || cx>=CHUNK_SIZE-borderMargin || cz>=CHUNK_SIZE-borderMargin) continue;
            //Use a deterministic per-cell hash RNG to avoid large coherent patches from low-frequency noise.
            //Keep Perlin to modulate some variability,but make placement primarily probabilistic per cell.
            float treeNoise=octavePerlin(worldX*0.25f+77.0f,worldZ*0.25f+91.0f,3,0.5f,2.0f,0.6f);
            //lower base probabilities to reduce overall tree density
            float spawnProb=0.0005f;
            if(biome==BIOME_FOREST) spawnProb=0.004f;//still denser in forests but not overcrowded
            else if(biome==BIOME_PLAINS) spawnProb=0.001f;
            else if(biome==BIOME_DESERT || biome==BIOME_BEACH || biome==BIOME_OCEAN) spawnProb=0;
            if(spawnProb<=0.0f) continue;

            // 确定性哈希 -> [0,1)
            float h = std::sin(worldX * 12.9898f + worldZ * 78.233f) * 43758.5453f;
            float cellRand = h - std::floor(h);

            // 根据噪声微调生成概率
            float noiseFactor = 1.5f + 0.5f * treeNoise; // ~[0,1]
            float effectiveProb = spawnProb * (0.5f + noiseFactor * 0.5f);
            if (cellRand >= effectiveProb) continue; // 不生成

            // 避免在 chunk 内将树生成得过于靠近
            const int minDist = 3; // 最小间距
            bool tooClose = false;
            int nx0 = std::max(0, cx - minDist);
            int nx1 = std::min(CHUNK_SIZE - 1, cx + minDist);
            int nz0 = std::max(0, cz - minDist);
            int nz1 = std::min(CHUNK_SIZE - 1, cz + minDist);
            int checkTop = std::min(CHUNK_HEIGHT - 1, topY + 6);
            for (int nx = nx0; nx <= nx1 && !tooClose; ++nx) {
                for (int nz = nz0; nz <= nz1; ++nz) {
                    for (int yy = topY; yy <= checkTop; ++yy) {
                        BlockType b = blocks[nx][yy][nz];
                        if (b == WOOD || b == LEAVES) { tooClose = true; break; }
                    }
                    if(tooClose) break;
                }
            }
            if(tooClose) continue;

            //树干高度：降低基准并减小可变范围，使树总体更矮小
            float hNoise=octavePerlin(worldX*0.17f+600.0f,worldZ*0.17f+600.0f,2,0.5f,2.0f,0.8f);
            //base 3,variable ~0..2 -> typical heights ~3..5
            int trunkH=4+static_cast<int>((hNoise+1.0f)*2.0f);
            trunkH=std::min(trunkH,CHUNK_HEIGHT-4-topY);
            bool canPlace=true;
            for(int y=topY;y<topY+trunkH;++y) {
                if(y>=CHUNK_HEIGHT || blocks[cx][y][cz]!=AIR) { canPlace=false;break;}
            }
            if(!canPlace) continue;
            for(int y=topY;y<topY+trunkH;++y) blocks[cx][y][cz]=WOOD;

            int leafBase=topY+trunkH-1;
            //叶冠：近似球/分层
            for(int lx=-3;lx<=3;++lx) {
                for(int lz=-3;lz<=3;++lz) {
                    for(int ly=0;ly<=3;++ly) {
                        int ax=cx+lx;
                        int ay=leafBase+ly;
                        int az=cz+lz;
                        if(ax<0 || ax>=CHUNK_SIZE || ay<0 || ay>=CHUNK_HEIGHT || az<0 || az>=CHUNK_SIZE) continue;
                        float dist2=(float)lx*lx+(float)lz*lz+(float)ly*ly*0.7f;
                        //更小的阈值，减少叶子扩展
                        if(dist2<=11.5f) {
                            if(blocks[ax][ay][az]==AIR) blocks[ax][ay][az]=LEAVES;
                        }
                    }
                }
            }
        }
    }

    //=== 云生成 ===
    const int CLOUD_BASE=CHUNK_HEIGHT-1;
    const int CLOUD_THICKNESS=1;
    const float CLOUD_SCALE=0.04f;
    const float CLOUD_THRESHOLD=0.38f;
    for(int cx=0;cx<CHUNK_SIZE;++cx) {
        for(int cz=0;cz<CHUNK_SIZE;++cz) {
            float worldX=static_cast<float>(chunkX*CHUNK_SIZE+cx);
            float worldZ=static_cast<float>(chunkZ*CHUNK_SIZE+cz);
            float n=octavePerlin(worldX*CLOUD_SCALE,worldZ*CLOUD_SCALE,3,0.5f,2.0f,1.0f);
            if(n>CLOUD_THRESHOLD) {
                for(int cy=0;cy<CLOUD_THICKNESS;++cy) {
                    int y=CLOUD_BASE+cy;
                    if(y>=0 && y<CHUNK_HEIGHT && blocks[cx][y][cz]==AIR) blocks[cx][y][cz]=CLOUD;
                }
            }
        }
    }

    needsUpdate=true;
}

//为指定方块在指定面生成六个顶点（两个三角形）并写入对应纹理组的顶点缓冲
void Chunk::addFace(float x,float y,float z,int face,BlockType type) {
    //选择纹理槽（草方块顶部/底部/侧面使用不同槽）
    int texIndex=-1;
    if(type==GRASS) {
        if(face==4) texIndex=0;      //草顶
        else if(face==5) texIndex=1; //底部使用泥土纹理
        else texIndex=7;                //侧面使用 grass_side
    } else {
        texIndex=blockTypeToTexIndex(type);
    }
    if(texIndex<0) return;

    //每个面的顶点原型（6 顶点，每顶点 3 分量）
    static const float v[6][18]={
        {0,0,1,1,0,1,1,1,1,  1,1,1,0,1,1,0,0,1},//前
        {1,0,0,0,0,0,0,1,0,  0,1,0,1,1,0,1,0,0},//后
        {0,0,0,0,0,1,0,1,1,  0,1,1,0,1,0,0,0,0},//左
        {1,0,1,1,0,0,1,1,0,  1,1,0,1,1,1,1,0,1},//右
        {0,1,1,1,1,1,1,1,0,  1,1,0,0,1,0,0,1,1},//上
        {0,0,0,1,0,0,1,0,1,  1,0,1,0,0,1,0,0,0} //下
    };

    static const glm::vec3 normals[6]={
        glm::vec3(0,0,1),glm::vec3(0,0,-1),glm::vec3(-1,0,0),glm::vec3(1,0,0),glm::vec3(0,1,0),glm::vec3(0,-1,0)
    };

    static const float texProto[12]={ 0,0,1,0,1,1,1,1,0,1,0,0 };

    auto &buf=verticesByType[texIndex];
    glm::vec3 normal=normals[face];

    //生成该面的 6 个顶点
    for(int vi=0;vi<6;++vi) {
        int pi=vi*3;
        glm::vec3 pos(x+v[face][pi],y+v[face][pi+1],z+v[face][pi+2]);
        glm::vec2 uv=computeTexCoords(type,face,pos,vi,texProto);
        pushVertex(buf,pos,uv,normal);
    }
}

//检查某个面是否可见（用于 Greedy Meshing）
bool Chunk::isFaceVisible(int x,int y,int z,int face,BlockType blockType) const {
     if(blockType==AIR) return false;
	 if(blockType==LEAVES)return true;

    // 计算邻居位置并判断邻居是否在本 chunk 内
    int wx = chunkX * CHUNK_SIZE + x;
    int wz = chunkZ * CHUNK_SIZE + z;
    int nLocalX = x, nLocalY = y, nLocalZ = z;
    int nChunkX = chunkX, nChunkZ = chunkZ;
    switch (face) {
        case 0: nLocalZ=z+1;
            if(nLocalZ>=CHUNK_SIZE) { 
                nLocalZ=0;
                nChunkZ=chunkZ+1;
            } 
            break;//+Z
        case 1: nLocalZ=z-1;
            if(nLocalZ<0) { 
                nLocalZ=CHUNK_SIZE-1;
                nChunkZ=chunkZ-1;
            } 
            break;//-Z
        case 2: nLocalX=x-1;
            if(nLocalX<0) { 
                nLocalX=CHUNK_SIZE-1;
                nChunkX=chunkX-1;
            } 
            break;//-X
        case 3: nLocalX=x+1;
            if(nLocalX>=CHUNK_SIZE) {
                nLocalX=0;
                nChunkX=chunkX+1;
            } 
            break;//+X
        case 4: 
            nLocalY=y+1;
            break; //+Y (Y is global within chunk)
        case 5: 
            nLocalY=y-1;
            break; //-Y
        default: 
            return false;
    }

    BlockType neighbor = AIR;
    if (nChunkX == chunkX && nChunkZ == chunkZ) {
        // 邻居在同一 chunk -> 快速局部查询
        neighbor = getBlock(nLocalX, nLocalY, nLocalZ);
    } else {
        // 邻居在不同 chunk。仅对水（常见水墙）执行跨区块查找。
        if (blockType == WATER) {
            int nx = nChunkX * CHUNK_SIZE + nLocalX;
            int nz = nChunkZ * CHUNK_SIZE + nLocalZ;
            // 如果邻居 chunk 未加载/创建，保守处理：假设邻居有水以避免产生面
            if (!world.hasChunkLoaded(nChunkX, nChunkZ)) {
                // 推迟发射面直到邻居 chunk 被创建，避免水面墙
                neighbor = WATER; // 视为水，这样不会发射此面
            } else {
                neighbor=world.getBlockIfLoaded(nx,nLocalY,nz);
            }
         } else {
             //assume absent neighbor (treat as AIR) to avoid expensive cross-chunk lookups foropaque blocks
             neighbor=AIR;
         }
    }

    return (neighbor==AIR || (blockType!=WATER && neighbor==WATER));
}

// 添加一个合并后的大四边形（width × height 个方块）
void Chunk::addQuad(float x,float y,float z,int width,int height,int face,BlockType type) {
    //选择纹理槽
    int texIndex=-1;
    if(type==GRASS) {
        if(face==4) texIndex=0;      //草顶
        else if(face==5) texIndex=1; //底部使用泥土纹理
        else texIndex=7;                //侧面使用 grass_side
    } else {
        texIndex=blockTypeToTexIndex(type);
    }
    if(texIndex<0) return;

    auto &buf=verticesByType[texIndex];
    
    //面法线
    static const glm::vec3 normals[6]={
        glm::vec3(0,0,1),glm::vec3(0,0,-1),glm::vec3(-1,0,0),
        glm::vec3(1,0,0),glm::vec3(0,1,0),glm::vec3(0,-1,0)
    };
    glm::vec3 normal=normals[face];
    
    //根据面的方向计算四边形的4个顶点
    glm::vec3 v0,v1,v2,v3;
    glm::vec2 uv0,uv1,uv2,uv3;
    
    switch (face) {
        case 0: //前面 +Z
            v0=glm::vec3(x,y,z+1);
            v1=glm::vec3(x+width,y,z+1);
            v2=glm::vec3(x+width,y+height,z+1);
            v3=glm::vec3(x,y+height,z+1);
            uv0=glm::vec2(0,0);
            uv1=glm::vec2(width,0);
            uv2=glm::vec2(width,height);
            uv3=glm::vec2(0,height);
            break;
        case 1: //后面 -Z
            v0=glm::vec3(x+width,y,z);
            v1=glm::vec3(x,y,z);
            v2=glm::vec3(x,y+height,z);
            v3=glm::vec3(x+width,y+height,z);
            uv0=glm::vec2(0,0);
            uv1=glm::vec2(width,0);
            uv2=glm::vec2(width,height);
            uv3=glm::vec2(0,height);
            break;
        case 2: //左面 -X
            v0=glm::vec3(x,y,z);
            v1=glm::vec3(x,y,z+width);
            v2=glm::vec3(x,y+height,z+width);
            v3=glm::vec3(x,y+height,z);
            uv0=glm::vec2(0,0);
            uv1=glm::vec2(width,0);
            uv2=glm::vec2(width,height);
            uv3=glm::vec2(0,height);
            break;
        case 3: //右面 +X
            v0=glm::vec3(x+1,y,z+width);
            v1=glm::vec3(x+1,y,z);
            v2=glm::vec3(x+1,y+height,z);
            v3=glm::vec3(x+1,y+height,z+width);
            uv0=glm::vec2(0,0);
            uv1=glm::vec2(width,0);
            uv2=glm::vec2(width,height);
            uv3=glm::vec2(0,height);
            break;
        case 4: //上面 +Y（恢复原始顶点顺序以保持剔除一致）
            v0=glm::vec3(x,y+1,z+height);
            v1=glm::vec3(x+width,y+1,z+height);
            v2=glm::vec3(x+width,y+1,z);
            v3=glm::vec3(x,y+1,z);
            uv0=glm::vec2(0,0);
            uv1=glm::vec2(width,0);
            uv2=glm::vec2(width,height);
            uv3=glm::vec2(0,height);
            break;
        case 5: //下面 -Y
            v0=glm::vec3(x,y,z);
            v1=glm::vec3(x+width,y,z);
            v2=glm::vec3(x+width,y,z+height);
            v3=glm::vec3(x,y,z+height);
            uv0=glm::vec2(0,0);
            uv1=glm::vec2(width,0);
            uv2=glm::vec2(width,height);
            uv3=glm::vec2(0,height);
            break;
    }
    
    //水面特殊处理：使用世界坐标UV
    if(type==WATER && face==4) {
        //直接用世界坐标/比例，避免 fract 引发插值伪影
        float uf0=v0.x/WATER_TILE_SIZE;
        float vf0=v0.z/WATER_TILE_SIZE;
        float uf1=v1.x/WATER_TILE_SIZE;
        float vf1=v1.z/WATER_TILE_SIZE;
        float uf2=v2.x/WATER_TILE_SIZE;
        float vf2=v2.z/WATER_TILE_SIZE;
        float uf3=v3.x/WATER_TILE_SIZE;
        float vf3=v3.z/WATER_TILE_SIZE;
        
        uv0=glm::vec2(uf0,vf0);
        uv1=glm::vec2(uf1,vf1);
        uv2=glm::vec2(uf2,vf2);
        uv3=glm::vec2(uf3,vf3);
    } else {
        //普通方块：纹理平铺
        uv1.x=uv2.x=static_cast<float>(width);
        uv2.y=uv3.y=static_cast<float>(height);
    }
    
    //生成两个三角形（6个顶点）组成四边形
    //三角形1: v0,v1,v2
    buf.push_back(v0.x);
    buf.push_back(v0.y);
    buf.push_back(v0.z);
    buf.push_back(uv0.x);
    buf.push_back(uv0.y);
    buf.push_back(normal.x);
    buf.push_back(normal.y);
    buf.push_back(normal.z);
    
    buf.push_back(v1.x);
    buf.push_back(v1.y);
    buf.push_back(v1.z);
    buf.push_back(uv1.x);
    buf.push_back(uv1.y);
    buf.push_back(normal.x);
    buf.push_back(normal.y);
    buf.push_back(normal.z);
    
    buf.push_back(v2.x);
    buf.push_back(v2.y);
    buf.push_back(v2.z);
    buf.push_back(uv2.x);
    buf.push_back(uv2.y);
    buf.push_back(normal.x);
    buf.push_back(normal.y);
    buf.push_back(normal.z);
    
    //三角形2: v2,v3,v0
    buf.push_back(v2.x);
    buf.push_back(v2.y);
    buf.push_back(v2.z);
    buf.push_back(uv2.x);
    buf.push_back(uv2.y);
    buf.push_back(normal.x);
    buf.push_back(normal.y);
    buf.push_back(normal.z);
    
    buf.push_back(v3.x);
    buf.push_back(v3.y);
    buf.push_back(v3.z);
    buf.push_back(uv3.x);
    buf.push_back(uv3.y);
    buf.push_back(normal.x);
    buf.push_back(normal.y);
    buf.push_back(normal.z);
    
    buf.push_back(v0.x);
    buf.push_back(v0.y);
    buf.push_back(v0.z);
    buf.push_back(uv0.x);
    buf.push_back(uv0.y);
    buf.push_back(normal.x);
    buf.push_back(normal.y);
    buf.push_back(normal.z);
}

//Greedy Meshing 主算法
void Chunk::buildGreedyMesh(const glm::vec3* viewDir,const glm::vec3* lightDir) {
    //清空之前的顶点数据
    for(int i=0;i<NUM_BLOCK_TEXTURES;++i) verticesByType[i].clear();

    //确定要检查的面
    bool checkFace[6]={false,false,false,false,false,false};
    
    if(viewDir==nullptr && lightDir==nullptr) {
        for(int i=0;i<6;++i) checkFace[i]=true;
        isFullMesh=true;
    } else {
        isFullMesh=false;
        if(viewDir!=nullptr) {
            checkFace[viewDir->z>0.0f ? 0 : 1]=true;
            checkFace[viewDir->x>0.0f ? 3 : 2]=true;
            checkFace[viewDir->y>0.0f ? 4 : 5]=true;
        }
        if(lightDir!=nullptr) {
            checkFace[lightDir->z>0.0f ? 0 : 1]=true;
            checkFace[lightDir->x>0.0f ? 3 : 2]=true;
            checkFace[lightDir->y>0.0f ? 4 : 5]=true;
        }
    }

    //对每个面方向执行 Greedy Meshing
    for(int face=0;face<6;++face) {
        if(!checkFace[face]) continue;
        
        //标记数组：记录哪些方块的该面已经被合并
        bool merged[CHUNK_SIZE][CHUNK_HEIGHT][CHUNK_SIZE]={{{false}}};
        
        //根据面的方向确定遍历顺序
        int dim1,dim2,dim3; //三个维度
        int size1,size2,size3;
        
        //面方向：0=前+Z,1=后-Z,2=左-X,3=右+X,4=上+Y,5=下-Y
        if(face==0 || face==1) {  //Z方向的面
            dim1=0;dim2=1;dim3=2; //X,Y,Z
            size1=CHUNK_SIZE;
            size2=CHUNK_HEIGHT;
            size3=CHUNK_SIZE;
        } else if(face==2 || face==3) {  //X方向的面
            dim1=2;dim2=1;dim3=0; //Z,Y,X
            size1=CHUNK_SIZE;
            size2=CHUNK_HEIGHT;
            size3=CHUNK_SIZE;
        } else {  //Y方向的面
            dim1=0;dim2=2;dim3=1; //X,Z,Y
            size1=CHUNK_SIZE;
            size2=CHUNK_SIZE;
            size3=CHUNK_HEIGHT;
        }
        
        //扫描该方向的所有层
        for(int d3=0;d3<size3;++d3) {
            for(int d2=0;d2<size2;++d2) {
                for(int d1=0;d1<size1;++d1) {
                    //转换回 x,y,z 坐标
                    int x,y,z;
                    if(face==0 || face==1) {
                        x=d1;y=d2;z=d3;
                    } else if(face==2 || face==3) {
                        x=d3;y=d2;z=d1;
                    } else {
                        x=d1;y=d3;z=d2;
                    }
                    
                    //如果已经合并过，跳过
                    if(merged[x][y][z]) continue;
                    
                    BlockType blockType=blocks[x][y][z];
                    if(blockType==AIR) continue;
                    if(!isFaceVisible(x,y,z,face,blockType)) continue;
                    
                    // 对透明/带 alpha 的方块（如 LEAVES, CLOUD）避免贪婪合并。
                    // 合并可能产生大平面覆盖区域，导致 alpha 边缘错误。
                    if (blockType == LEAVES || blockType == CLOUD) {
                        merged[x][y][z] = true;
                        // 世界坐标
                        float wx = static_cast<float>(chunkX * CHUNK_SIZE + x);
                        float wy = static_cast<float>(y);
                        float wz = static_cast<float>(chunkZ * CHUNK_SIZE + z);
                        addFace(wx, wy, wz, face, blockType);
                        continue;
                    }

                    //开始贪婪扩展
                    //1. 向 dim1 方向扩展（宽度）
                    int width=1;
                    while (d1+width<size1) {
                        int nx,ny,nz;
                        if(face==0 || face==1) {
                            nx=d1+width;ny=d2;nz=d3;
                        } else if(face==2 || face==3) {
                            nx=d3;ny=d2;nz=d1+width;
                        } else {
                            nx=d1+width;ny=d3;nz=d2;
                        }
                        
                        if(merged[nx][ny][nz]) break;
                        if(blocks[nx][ny][nz]!=blockType) break;
                        if(!isFaceVisible(nx,ny,nz,face,blockType)) break;
                        
                        width++;
                    }
                    
                    //2. 向 dim2 方向扩展（高度）
                    int height=1;
                    bool canExtend=true;
                    while (d2+height<size2 && canExtend) {
                        //检查该行的所有方块是否都可以合并
                        for(int w=0;w<width;++w) {
                            int nx,ny,nz;
                            if(face==0 || face==1) {
                                nx=d1+w;
                                ny=d2+height;
                                nz=d3;
                            } else if(face==2 || face==3) {
                                nx=d3;
                                ny=d2+height;
                                nz=d1+w;
                            } else {
                                nx=d1+w;
                                ny=d3;
                                nz=d2+height;
                            }
                            
                            if(merged[nx][ny][nz] || 
                                blocks[nx][ny][nz]!=blockType ||
                                !isFaceVisible(nx,ny,nz,face,blockType)) {
                                canExtend=false;
                                break;
                            }
                        }
                        if(canExtend) height++;
                    }
                    
                    //3. 标记所有合并的方块
                    for(int h=0;h<height;++h) {
                        for(int w=0;w<width;++w) {
                            int nx,ny,nz;
                            if(face==0 || face==1) {
                                nx=d1+w;
                                ny=d2+h;
                                nz=d3;
                            } else if(face==2 || face==3) {
                                nx=d3;
                                ny=d2+h;
                                nz=d1+w;
                            } else {
                                nx=d1+w;
                                ny=d3;
                                nz=d2+h;
                            }
                            merged[nx][ny][nz]=true;
                        }
                    }
                    
                    //4. 生成合并后的大四边形
                    float wx=static_cast<float>(chunkX*CHUNK_SIZE+x);
                    float wy=static_cast<float>(y);
                    float wz=static_cast<float>(chunkZ*CHUNK_SIZE+z);
                    addQuad(wx,wy,wz,width,height,face,blockType);
                }
            }
        }
    }

    needsUpdate=false;
}

//为 chunk 构建网格（按纹理分组）并上传到 GPU
void Chunk::buildMesh(const glm::vec3* viewDir,const glm::vec3* lightDir) {
    //构建完整网格，避免选择性面导致缺失地面或露出地下
    buildGreedyMesh(nullptr,nullptr);
    ensureGpuLoaded();

    //将每个纹理组的顶点上传到 GPU，并设置顶点属性指针
    for(int i=0;i<NUM_BLOCK_TEXTURES;++i) {
        glBindVertexArray(VAOs[i]);
        glBindBuffer(GL_ARRAY_BUFFER,VBOs[i]);
        if(!verticesByType[i].empty()) {
            glBufferData(GL_ARRAY_BUFFER,verticesByType[i].size()*sizeof(float),verticesByType[i].data(),GL_STATIC_DRAW);
            int stride=8*sizeof(float);//pos(3) tex(2) normal(3)
            glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,stride,(void*)0);
            glEnableVertexAttribArray(0);
            glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,stride,(void*)(3*sizeof(float)));
            glEnableVertexAttribArray(1);
            glVertexAttribPointer(2,3,GL_FLOAT,GL_FALSE,stride,(void*)(5*sizeof(float)));
            glEnableVertexAttribArray(2);
        }
    }
}

//渲染：按每个纹理组绑定对应纹理并绘制
void Chunk::renderOpaque(Shader &shader,const glm::vec3* lightDir) {
    if(needsUpdate && !pendingBuild) buildMesh(nullptr,lightDir);

    //Opaque pass: draw all non-transparent texture groups and update depth buffer
    for(int i=0;i<NUM_BLOCK_TEXTURES;++i) {
        //indices 4=leaves,6=water,8=cloud are transparent and should not be in opaque pass
        if(i==4 || i==6 || i==8) continue;
        if(verticesByType[i].empty()) continue;
        float specular=0.1f;
        if(i==6) specular=1.0f;
        else if(i==5 || i==0 || i==7) specular=0.0f;
        else if(i==2) specular=0.05f;
        shader.setFloat("specularStrength",specular);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D,blockTextures[i]);
        glBindVertexArray(VAOs[i]);
        glDrawArrays(GL_TRIANGLES,0,static_cast<GLsizei>(verticesByType[i].size()/8));
    }
}

//Note: per-chunk transparent draw is unused when World performs global sorting.
void Chunk::renderTransparent(Shader &shader,const glm::vec3* viewDir,const glm::vec3* cameraPos,const glm::vec3* lightDir) {
}

void Chunk::renderDepth(Shader &depthShader) {
    if((needsUpdate || !isFullMesh) && !pendingBuild) {
        buildMesh(nullptr,nullptr);
    }
    
    for(int i=0;i<NUM_BLOCK_TEXTURES;++i) {
        if(verticesByType[i].empty()) continue;
        glBindVertexArray(VAOs[i]);
        glDrawArrays(GL_TRIANGLES,0,static_cast<GLsizei>(verticesByType[i].size()/8));
    }
}

//New: CPU-only mesh generation returning MeshData (safe to call from worker thread)
MeshData Chunk::buildMeshCPU(const glm::vec3* viewDir,const glm::vec3* lightDir) {
    MeshData out;out.chunkX=chunkX;out.chunkZ=chunkZ;
    std::vector<float> tempBuffers[NUM_BLOCK_TEXTURES];
    //Always process all 6 faces forCPU mesh
    for(int face=0;face<6;++face) {
        bool merged[CHUNK_SIZE][CHUNK_HEIGHT][CHUNK_SIZE]={{{false}}};
        int size1,size2,size3;//traversal dimensions mapping
        if(face==0 || face==1) { 
            size1=CHUNK_SIZE;
            size2=CHUNK_HEIGHT;
            size3=CHUNK_SIZE;
        }else if(face==2 || face==3) {
            size1=CHUNK_SIZE;
            size2=CHUNK_HEIGHT;
            size3=CHUNK_SIZE;
        }else { 
            size1=CHUNK_SIZE;
            size2=CHUNK_SIZE;
            size3=CHUNK_HEIGHT;
        }
        for(int d3=0;d3<size3;++d3) {
            for(int d2=0;d2<size2;++d2) {
                for(int d1=0;d1<size1;++d1) {
                    int x,y,z;
                    if(face==0 || face==1) {
                        x=d1;y=d2;z=d3;
                    }else if(face==2 || face==3) {
                        x=d3;y=d2;z=d1;
                    }else{ x=d1;y=d3;z=d2;}
                    if(merged[x][y][z]) continue;
                    BlockType bt=blocks[x][y][z];
                    if(bt==AIR) continue;
                    if(!isFaceVisible(x,y,z,face,bt)) continue;
                    //width expansion
                    int width=1;
                    while (d1+width<size1) {
                        int nx,ny,nz;
                        if(face==0||face==1){ 
                            nx=d1+width;
                            ny=d2;
                            nz=d3;
                        } else if(face==2||face==3){ 
                            nx=d3;
                            ny=d2;
                            nz=d1+width;
                        } else {
                            nx=d1+width;
                            ny=d3;
                            nz=d2;
                        }
                        if(merged[nx][ny][nz]) break;
                        if(blocks[nx][ny][nz]!=bt) break;
                        if(!isFaceVisible(nx,ny,nz,face,bt)) break;
                        ++width;
                    }
                    //height expansion
                    int height=1;
                    bool canExtend=true;
                    while(canExtend && d2+height<size2) {
                        for(int w=0;w<width;++w){ 
                            int nx,ny,nz;
                            if(face==0||face==1){ 
                                nx=d1+w;
                                ny=d2+height;
                                nz=d3;
                            } else if(face==2||face==3){
                                nx=d3;
                                ny=d2+height;
                                nz=d1+w;
                            } else { 
                                nx=d1+w;
                                ny=d3;
                                nz=d2+height;
                            }
                            if(merged[nx][ny][nz] || blocks[nx][ny][nz]!=bt || !isFaceVisible(nx,ny,nz,face,bt)){ 
                                canExtend=false;
                                break;
                            }
                        }
                        if(canExtend) ++height;
                    }
                    //mark merged
                    for(int h=0;h<height;++h){ 
                        for(int w=0;w<width;++w){
                            int nx,ny,nz;
                            if(face==0||face==1){ 
                                nx=d1+w;
                                ny=d2+h;
                                nz=d3;
                            } else if(face==2||face==3){ 
                                nx=d3;
                                ny=d2+h;
                                nz=d1+w;
                            } else {
                                nx=d1+w;
                                ny=d3;
                                nz=d2+h;
                            } merged[nx][ny][nz]=true;
                        }
                    }
                    //emit quad
                    float wx=static_cast<float>(chunkX*CHUNK_SIZE+x);
                    float wy=static_cast<float>(y);
                    float wz=static_cast<float>(chunkZ*CHUNK_SIZE+z);
                    //local quad generation (duplicate of addQuad simplified)
                    int texIndex=-1;
                    if(bt==GRASS){
                        if(face==4) texIndex=0;
                        else if(face==5) texIndex=1;
                        else texIndex=7;
                    } else texIndex=blockTypeToTexIndex(bt);
                    if(texIndex<0) continue;
                    auto &buf=tempBuffers[texIndex];
                    static const glm::vec3 normals[6]={ {0,0,1},{0,0,-1},{-1,0,0},{1,0,0},{0,1,0},{0,-1,0} };
                    glm::vec3 normal=normals[face];
                    glm::vec3 v0,v1,v2,v3;
                    switch(face){
                        case 0: v0={wx,wy,wz+1};
                            v1={wx+width,wy,wz+1};
                            v2={wx+width,wy+height,wz+1};
                            v3={wx,wy+height,wz+1};
                            break;
                        case 1: 
                            v0={wx+width,wy,wz};
                            v1={wx,wy,wz};
                            v2={wx,wy+height,wz};
                            v3={wx+width,wy+height,wz};
                            break;
                        case 2: 
                            v0={wx,wy,wz};
                            v1={wx,wy,wz+width};
                            v2={wx,wy+height,wz+width};
                            v3={wx,wy+height,wz};
                            break;
                        case 3:
                            v0={wx+1,wy,wz+width};
                            v1={wx+1,wy,wz};
                            v2={wx+1,wy+height,wz};
                            v3={wx+1,wy+height,wz+width};
                            break;
                        case 4: 
                            v0={wx,wy+1,wz+height};
                            v1={wx+width,wy+1,wz+height};
                            v2={wx+width,wy+1,wz};
                            v3={wx,wy+1,wz};
                            break;
                        case 5: 
                            v0={wx,wy,wz};
                            v1={wx+width,wy,wz};
                            v2={wx+width,wy,wz+height};
                            v3={wx,wy,wz+height};
                            break;
                    }
                    glm::vec2 uv0(0,0),uv1(width,0),uv2(width,height),uv3(0,height);
                    if(bt==WATER && face==4){
                        uv0={v0.x/WATER_TILE_SIZE,v0.z/WATER_TILE_SIZE};
                        uv1={v1.x/WATER_TILE_SIZE,v1.z/WATER_TILE_SIZE};
                        uv2={v2.x/WATER_TILE_SIZE,v2.z/WATER_TILE_SIZE};
                        uv3={v3.x/WATER_TILE_SIZE,v3.z/WATER_TILE_SIZE};
                    }
                    pushVertex(buf,v0,uv0,normal);
                    pushVertex(buf,v1,uv1,normal);
                    pushVertex(buf,v2,uv2,normal);
                    pushVertex(buf,v2,uv2,normal);
                    pushVertex(buf,v3,uv3,normal);
                    pushVertex(buf,v0,uv0,normal);
                }
            }
        }
    }
    for(int i=0;i<NUM_BLOCK_TEXTURES;++i) out.verticesByType[i]=std::move(tempBuffers[i]);
    return out;
}

void Chunk::uploadMeshFromData(const MeshData& data) {
    if(data.chunkX!=chunkX || data.chunkZ!=chunkZ) return;
    for(int i=0;i<NUM_BLOCK_TEXTURES;++i) verticesByType[i]=data.verticesByType[i];
    ensureGpuLoaded();
    for(int i=0;i<NUM_BLOCK_TEXTURES;++i) {
        glBindVertexArray(VAOs[i]);
        glBindBuffer(GL_ARRAY_BUFFER,VBOs[i]);
        if(!verticesByType[i].empty()) {
            glBufferData(GL_ARRAY_BUFFER,verticesByType[i].size()*sizeof(float),nullptr,GL_STATIC_DRAW);
            glBufferSubData(GL_ARRAY_BUFFER,0,verticesByType[i].size()*sizeof(float),verticesByType[i].data());
            int stride=8*sizeof(float);
            glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,stride,(void*)0);
            glEnableVertexAttribArray(0);
            glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,stride,(void*)(3*sizeof(float)));
            glEnableVertexAttribArray(1);
            glVertexAttribPointer(2,3,GL_FLOAT,GL_FALSE,stride,(void*)(5*sizeof(float)));
            glEnableVertexAttribArray(2);
        }
    }
    needsUpdate=false;
    pendingBuild=false;
}

//Collect transparent faces forglobal sorting. Each tuple: (depth,texIndex,faceIndex,chunkPtr)
void Chunk::collectTransparentFaces(std::vector<std::tuple<float,int,int,Chunk*>> &out,const glm::vec3* viewDir,const glm::vec3* cameraPos) const {
    const size_t floatsPerVertex=8;
    //collect leaves (4),water (6) and cloud (8) as transparent faces forglobal sorting
    for(int i : {4,6,8}) {
        if(i<0 || i>=NUM_BLOCK_TEXTURES) continue;
        const auto &buf=verticesByType[i];
        if(buf.empty()) continue;
        size_t totalFloats=buf.size();
        if(totalFloats % floatsPerVertex!=0) continue;
        size_t totalVertices=totalFloats/floatsPerVertex;
        if(totalVertices % 6!=0) continue;
        size_t faceCount=totalVertices/6;
        for(size_t f=0;f<faceCount;++f) {
            glm::vec3 centroid(0.0f);
            for(int v=0;v<6;++v) {
                size_t vi=(f*6+v)*floatsPerVertex;
                float px=buf[vi+0];
                float py=buf[vi+1];
                float pz=buf[vi+2];
                centroid += glm::vec3(px,py,pz);
            }
            centroid /= 6.0f;
            float depth=0.0f;
            if(viewDir!=nullptr) {
                if(cameraPos!=nullptr) depth=glm::dot(centroid-*cameraPos,*viewDir);
                else depth=glm::dot(centroid,*viewDir);
            } else depth=centroid.z;
            out.emplace_back(depth,i,static_cast<int>(f),const_cast<Chunk*>(this));
        }
    }
}

// 绘制单个透明面（绑定纹理/VAO 并绘制 faceIndex 的 6 个顶点）
void Chunk::drawTransparentFace(int texIndex,int faceIndex) const {
    if(texIndex<0 || texIndex>=NUM_BLOCK_TEXTURES) return;
    if(verticesByType[texIndex].empty()) return;
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D,blockTextures[texIndex]);
    glBindVertexArray(VAOs[texIndex]);
    glDrawArrays(GL_TRIANGLES,faceIndex*6,6);
}