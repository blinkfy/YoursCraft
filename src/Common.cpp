#include "../include/Common.h"

//存放外部声明的纹理句柄
GLuint blockTextures[NUM_BLOCK_TEXTURES];
GLuint panoramaTextures[6]={ 0 };
GLuint titleTexture=0;
GLuint subtitleTexture=0;
GLuint sphereTexture=0;

int blockTypeToTexIndex(BlockType t) {
    switch (t) {
    case GRASS: return 7;//侧面
    case DIRT: return 1;
    case STONE: return 2;
    case WOOD: return 3;
    case LEAVES: return 4;
    case SAND: return 5;
    case WATER: return 6;
    case CLOUD: return 8;
    default: return -1;
    }
}

//噪声/地形函数
float octavePerlin(float x,float z,int octaves,float persistence,float lacunarity,float scale) {
    float tot=0.0f;
    float freq=scale;
    float amp=1.0f;
    float maxv=0.0f;
    for(int i=0;i<octaves;++i) {
        tot += glm::perlin(glm::vec2(x*freq,z*freq))*amp;
        maxv += amp;
        amp *= persistence;
        freq *= lacunarity;
    }
    return tot/maxv;
}

float smoothLerp(float a,float b,float t) {
    t=t*t*(3.0f-2.0f*t);
    return a+t*(b-a);
}

float calculateTerrainHeight(float worldX,float worldZ) {
    float cNoise=octavePerlin(worldX,worldZ,4,0.5f,2.0f,CONTINENT_SCALE);
    cNoise=(cNoise+1.0f)*0.5f;
    float tNoise=octavePerlin(worldX,worldZ,6,0.5f,2.0f,TERRAIN_SCALE);
    float mNoise=octavePerlin(worldX+1000.0f,worldZ+1000.0f,5,0.6f,2.2f,MOUNTAIN_SCALE);
    mNoise=std::abs(mNoise);
    mNoise=1.0f-mNoise;
    mNoise=std::pow(mNoise,2.5f);
    float dNoise=octavePerlin(worldX,worldZ,3,0.4f,2.5f,DETAIL_SCALE)*3.0f;
    float eNoise=octavePerlin(worldX+5000.0f,worldZ+5000.0f,3,0.5f,2.0f,EROSION_SCALE);
    eNoise=(eNoise+1.0f)*0.5f;

    float contH=smoothLerp(MIN_TERRAIN_HEIGHT,MAX_TERRAIN_HEIGHT-10,cNoise);
    float terrainH=tNoise*15.0f;
    float mountainH=mNoise*25.0f*cNoise;
    float erosionF=smoothLerp(0.7f,1.0f,eNoise);
    float finalH=(contH+terrainH+mountainH+dNoise)*erosionF;
    finalH=std::max((float)MIN_TERRAIN_HEIGHT,std::min((float)MAX_TERRAIN_HEIGHT,finalH));
    return finalH;
}

BiomeType getBiome(float worldX,float worldZ,float height) {
    float temp=octavePerlin(worldX,worldZ,3,0.5f,2.0f,0.02f);
    temp=(temp+1.0f)*0.5f;
    float moist=octavePerlin(worldX+10000.0f,worldZ+10000.0f,3,0.5f,2.0f,0.025f);
    moist=(moist+1.0f)*0.5f;
    if(height<WATER_LEVEL-2) return BIOME_OCEAN;
    if(height<WATER_LEVEL+2) return BIOME_BEACH;
    if(height>55) return BIOME_MOUNTAINS;
    if(height>50 && temp<0.3f) return BIOME_SNOW;
    if(temp>0.7f && moist<0.3f) return BIOME_DESERT;
    if(moist>0.6f && temp>0.4f) return BIOME_FOREST;
    return BIOME_PLAINS;
}
