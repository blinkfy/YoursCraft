#include "../include/Common.h"
#include "../include/Texture.h"

#include <thread>
#include <atomic>
#include <mutex>
#include <queue>
#include <chrono>
#include <vector>
#include <string>
#include <fstream>
#include <algorithm>
#include <cstdlib>

//前向声明
void stopTextureLoader();

//常量
constexpr int PANORAMA_COUNT=6;

//文件列表
static const std::vector<std::string> g_panoFaces={
    "minecraft_textures/gui/title/background/panorama_0.png",
    "minecraft_textures/gui/title/background/panorama_1.png",
    "minecraft_textures/gui/title/background/panorama_2.png",
    "minecraft_textures/gui/title/background/panorama_3.png",
    "minecraft_textures/gui/title/background/panorama_4.png",
    "minecraft_textures/gui/title/background/panorama_5.png"
};

static const std::string g_titleTexturePath="minecraft_textures/gui/title/yourscraft.png";
static const std::string g_subtitleTexturePath="minecraft_textures/gui/title/yourcraft_ch.png";

static const std::vector<std::string> g_blockNames={
    "grass_top.png",
    "dirt.png",
    "stone.png",
    "planks_oak.png",
    "leaves_oak.png",
    "sand.png",
    "water.png",
    "grass_side.png",
    "cloud.png"
};

static const std::string g_sphereTextureName="soccer.png";

static const std::vector<std::string> g_blockSearchDirs={
    "minecraft_textures/blocks/",
    "minecraft_textures/"
    ""
};

//待上传图片数据结构
struct PendingImage {
    int texIndex=-1;
    int imageType=0;//0 -> panoramaTextures[],1 -> blockTextures[],2 -> titleTexture,3 -> subtitleTexture,4 -> sphereTexture
    std::vector<unsigned char> data;
    int width=0,height=0,channels=0;
};

//异步加载相关队列与同步
static std::queue<PendingImage> g_pendingUploads;
static std::mutex g_pendingMutex;

static std::queue<int> g_panoRequests;
static std::queue<int> g_blockRequests;
static std::queue<int> g_specialRequests;//title/subtitle/sphere 特殊请求
static std::mutex g_requestMutex;

static std::atomic<bool> g_loaderRunning{ false };
static std::thread* g_loaderThread=nullptr;

static void enqueuePendingUpload(PendingImage&& img) {
    std::lock_guard<std::mutex> lk(g_pendingMutex);
    g_pendingUploads.push(std::move(img));
}

void requestPanoramaLoad(int index) {
    std::lock_guard<std::mutex> lk(g_requestMutex);
    if(index>=0 && index<(int)g_panoFaces.size())
        g_panoRequests.push(index);
}

void requestBlockTextureLoad(int index) {
    std::lock_guard<std::mutex> lk(g_requestMutex);
    if(index>=0 && index<(int)g_blockNames.size())
        g_blockRequests.push(index);
}

void requestTitleTextureLoad() {
    std::lock_guard<std::mutex> lk(g_requestMutex);
    g_specialRequests.push(-1);//title
}

void requestSubtitleTextureLoad() {
    std::lock_guard<std::mutex> lk(g_requestMutex);
    g_specialRequests.push(-2);//subtitle
}

void requestSphereTextureLoad() {
    std::lock_guard<std::mutex> lk(g_requestMutex);
    g_specialRequests.push(-3);//sphere
}

static void doLoadAndEnqueue(const std::string& path,int texIndex,int imageType) {
    int w=0,h=0,c=0;
    unsigned char* data=SOIL_load_image(path.c_str(),&w,&h,&c,SOIL_LOAD_AUTO);
    if(!data) { std::cerr << "SOIL failed to load: " << path << "\n";return;}
    PendingImage img;
    img.texIndex=texIndex;
    img.imageType=imageType;
    img.width=w;img.height=h;img.channels=c;
    img.data.assign(data,data+(w*h*c));
    //垂直翻转
    int rowSize=w*c;
    for(int y=0;y<h/2;++y) {
        unsigned char* top=img.data.data()+y*rowSize;
        unsigned char* bot=img.data.data()+(h-1-y)*rowSize;
        for(int x=0;x<rowSize;++x) std::swap(top[x],bot[x]);
    }
    SOIL_free_image_data(data);
    enqueuePendingUpload(std::move(img));
}

static void loaderThreadMain() {
    while (g_loaderRunning) {
        int pidx=-4; //-4 无请求, >=0 全景索引
        int bidx=-1;
        int sidx=-4;//特殊: -1 title,-2 subtitle,-3 sphere
        {
            std::lock_guard<std::mutex> lk(g_requestMutex);
            if(!g_panoRequests.empty()) { 
                pidx=g_panoRequests.front();
                g_panoRequests.pop();
            }else if(!g_blockRequests.empty()){ 
                bidx=g_blockRequests.front();
                g_blockRequests.pop();
            }else if(!g_specialRequests.empty()) {
                sidx=g_specialRequests.front();
                g_specialRequests.pop();
            }
        }
        if(sidx==-2) {
            std::ifstream f(g_subtitleTexturePath);
            if(f.good()) {
                f.close();
                doLoadAndEnqueue(g_subtitleTexturePath,0,3);
            }
        }
        else if(sidx==-1) {
            std::ifstream f(g_titleTexturePath);
            if(f.good()) {
                f.close();
            doLoadAndEnqueue(g_titleTexturePath,0,2);
            }
        }
        else if(sidx==-3) {
            //球体纹理
            std::string found;
            for(auto &d : g_blockSearchDirs) {
                std::string p=d+g_sphereTextureName;
                std::ifstream f(p);
                if(f.good()) { 
                    f.close();
                    found=p;
                    break;
                }
            }
            if(!found.empty()) doLoadAndEnqueue(found,0,4);
        }
        else if(pidx>=0) {
            //加载全景纹理
            const std::string& p=g_panoFaces[pidx];
            std::ifstream f(p);
            if(f.good()) { f.close();doLoadAndEnqueue(p,pidx,0);}
        }
        else if(bidx>=0) {
            //加载方块纹理
            std::string found;
            for(auto& d : g_blockSearchDirs) {
                std::string p=d+g_blockNames[bidx];
                std::ifstream f(p);
                if(f.good()) {
                    f.close();
                    found=p;
                    break;
                }
            }
            if(!found.empty()) doLoadAndEnqueue(found,bidx,1);
        }
        else {
            //无请求，短暂休眠
            std::this_thread::sleep_for(std::chrono::milliseconds(8));
        }
    }
}

void startTextureLoader() {
    if(g_loaderRunning) return;
    g_loaderRunning=true;
    g_loaderThread=new std::thread(loaderThreadMain);
    static bool atexitRegistered=false;
    if(!atexitRegistered) {
        std::atexit(stopTextureLoader);
        atexitRegistered=true;
    }
}

void stopTextureLoader() {
    if(!g_loaderRunning) return;
    g_loaderRunning=false;
    if(g_loaderThread) {
        if(g_loaderThread->joinable()) g_loaderThread->join();
        delete g_loaderThread;g_loaderThread=nullptr;
    }
}

void processPendingTextureUploads(int maxUploads) {
    int count=0;
    while (count<maxUploads) {
        PendingImage img;
        {
            std::lock_guard<std::mutex> lk(g_pendingMutex);
            if(g_pendingUploads.empty()) break;
            img=std::move(g_pendingUploads.front());
            g_pendingUploads.pop();
        }
        GLenum format=(img.channels==4) ? GL_RGBA : GL_RGB;
        GLuint tex=0;
        if(img.imageType==1) tex=blockTextures[img.texIndex];
        else if(img.imageType==2) tex=titleTexture;
        else if(img.imageType==3) tex=subtitleTexture;
        else if(img.imageType==4) tex=sphereTexture;
        else tex=panoramaTextures[img.texIndex];
        //如果目标纹理句柄尚未创建(==0)，重新排队并停止
        if(tex==0) {
            std::lock_guard<std::mutex> lk(g_pendingMutex);
            g_pendingUploads.push(std::move(img));
            break;
        }
        glBindTexture(GL_TEXTURE_2D,tex);
        glPixelStorei(GL_UNPACK_ALIGNMENT,1);
        glTexImage2D(GL_TEXTURE_2D,0,format,img.width,img.height,0,format,GL_UNSIGNED_BYTE,img.data.data());

        //根据类型设置纹理参数
        if(img.imageType==1) {
            //方块纹理：最近邻过滤
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_REPEAT);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_REPEAT);
        }
        else {
            //全景/标题/副标题：线性过滤
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
        }
        count++;
    }
}

bool hasPendingTextureUploads() {
    std::lock_guard<std::mutex> lk(g_pendingMutex);
    return !g_pendingUploads.empty();
}

//传统同步加载器
void loadBlockTextures() {
    glGenTextures(NUM_BLOCK_TEXTURES,blockTextures);
    for(int i=0;i<NUM_BLOCK_TEXTURES;++i) {
        glBindTexture(GL_TEXTURE_2D,blockTextures[i]);
        std::string found;
        for(auto& d : g_blockSearchDirs) {
            std::string p=d+g_blockNames[i];
            std::ifstream f(p);
            if(f.good()) { 
                f.close();
                found=p;
                break;
            }
        }
        if(!found.empty()){
            int w=0,h=0,c=0;unsigned char* data=SOIL_load_image(found.c_str(),&w,&h,&c,SOIL_LOAD_AUTO);
            if(data) {
                int rowSize=w*c;
                for(int y=0;y<h/2;++y) { 
                    unsigned char* top=data+y*rowSize;
                    unsigned char* bot=data+(h-1-y)*rowSize;
                    for(int x=0;x<rowSize;++x) 
                        std::swap(top[x],bot[x]);
                }
                GLenum format=(c==4) ? GL_RGBA : GL_RGB;glPixelStorei(GL_UNPACK_ALIGNMENT,1);
                glTexImage2D(GL_TEXTURE_2D,0,format,w,h,0,format,GL_UNSIGNED_BYTE,data);
                SOIL_free_image_data(data);
            }else{ 
                unsigned char pink[3]={ 255,0,255 };
                glTexImage2D(GL_TEXTURE_2D,0,GL_RGB,1,1,0,GL_RGB,GL_UNSIGNED_BYTE,pink);
            }
        }else{ 
            unsigned char pink[3]={ 255,0,255 };
            glTexImage2D(GL_TEXTURE_2D,0,GL_RGB,1,1,0,GL_RGB,GL_UNSIGNED_BYTE,pink);
        }

        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    }
}

void loadPanoramaTextures() {
    glGenTextures(PANORAMA_COUNT,panoramaTextures);
    for(int i=0;i<PANORAMA_COUNT;++i) {
        glBindTexture(GL_TEXTURE_2D,panoramaTextures[i]);
        std::string p=g_panoFaces[i];std::ifstream f(p);
        if(!f.good()) { 
            unsigned char pink[3]={ 255,0,255 };
            glTexImage2D(GL_TEXTURE_2D,0,GL_RGB,1,1,0,GL_RGB,GL_UNSIGNED_BYTE,pink);
            continue;
        }
        f.close();
        int w=0,h=0,c=0;
        unsigned char* data=SOIL_load_image(p.c_str(),&w,&h,&c,SOIL_LOAD_AUTO);
        if(data) { 
            int rowSize=w*c;
            for(int y=0;y<h/2;++y) { 
                unsigned char* top=data+y*rowSize;
                unsigned char* bot=data+(h-1-y)*rowSize;
                for(int x=0;x<rowSize;++x) 
                    std::swap(top[x],bot[x]);
            } GLenum format=(c==4) ? GL_RGBA : GL_RGB;glPixelStorei(GL_UNPACK_ALIGNMENT,1);
            glTexImage2D(GL_TEXTURE_2D,0,format,w,h,0,format,GL_UNSIGNED_BYTE,data);
            SOIL_free_image_data(data);
        }else{
            unsigned char pink[3]={ 255,0,255 };
            glTexImage2D(GL_TEXTURE_2D,0,GL_RGB,1,1,0,GL_RGB,GL_UNSIGNED_BYTE,pink);
        }

        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    }
}

void initPanoramaTextureHandles() {
    glGenTextures(PANORAMA_COUNT,panoramaTextures);
    unsigned char gray[3]={ 150,150,150 };
    for(int i=0;i<PANORAMA_COUNT;++i) {
        glBindTexture(GL_TEXTURE_2D,panoramaTextures[i]);
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGB,1,1,0,GL_RGB,GL_UNSIGNED_BYTE,gray);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    }
}

void initBlockTextureHandles() {
    glGenTextures(NUM_BLOCK_TEXTURES,blockTextures);
    unsigned char gray[3]={ 180,180,180 };
    for(int i=0;i<NUM_BLOCK_TEXTURES;++i) {
        glBindTexture(GL_TEXTURE_2D,blockTextures[i]);
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGB,1,1,0,GL_RGB,GL_UNSIGNED_BYTE,gray);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_REPEAT);
    }
}

void initSphereTextureHandle() {
    glGenTextures(1,&sphereTexture);
    unsigned char gray[3]={ 200,200,200 };
    glBindTexture(GL_TEXTURE_2D,sphereTexture);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGB,1,1,0,GL_RGB,GL_UNSIGNED_BYTE,gray);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_REPEAT);
}

void initTitleTextureHandle() {
    glGenTextures(1,&titleTexture);
    glBindTexture(GL_TEXTURE_2D,titleTexture);
    unsigned char transparent[4]={ 0,0,0,0 };
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,1,1,0,GL_RGBA,GL_UNSIGNED_BYTE,transparent);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
}

void initSubtitleTextureHandle() {
    glGenTextures(1,&subtitleTexture);
    glBindTexture(GL_TEXTURE_2D,subtitleTexture);
    unsigned char transparent[4]={ 0,0,0,0 };
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,1,1,0,GL_RGBA,GL_UNSIGNED_BYTE,transparent);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
}

bool loadPanoramaTextureIndex(int index) {
    if(index<0 || index>=PANORAMA_COUNT) return false;
    std::string p=g_panoFaces[index];std::ifstream f(p);
    if(!f.good()) return false;f.close();int w=0,h=0,c=0;
    unsigned char* data=SOIL_load_image(p.c_str(),&w,&h,&c,SOIL_LOAD_AUTO);
    if(!data) return false;
    PendingImage img;
    img.texIndex=index;
    img.imageType=0;
    img.width=w;
    img.height=h;
    img.channels=c;
    img.data.assign(data,data+(w*h*c));
    int rowSize=w*c;
    for(int y=0;y<h/2;++y) { 
        unsigned char* top=img.data.data()+y*rowSize;
        unsigned char* bot=img.data.data()+(h-1-y)*rowSize;
        for(int x=0;x<rowSize;++x) std::swap(top[x],bot[x]);
    } SOIL_free_image_data(data);
    enqueuePendingUpload(std::move(img));
    return true;
}

bool loadBlockTextureIndex(int index) {
    if(index<0 || index>=NUM_BLOCK_TEXTURES) return false;
    std::string found;
    for(auto& d : g_blockSearchDirs) { 
        std::string p=d+g_blockNames[index];
        std::ifstream f(p);
        if(f.good()) {
            f.close();
            found=p;
            break;
        } 
    }
    if(found.empty()) return false;
    int w=0,h=0,c=0;
    unsigned char* data=SOIL_load_image(found.c_str(),&w,&h,&c,SOIL_LOAD_AUTO);
    if(!data) return false;
    PendingImage img;
    img.texIndex=index;
    img.imageType=1;
    img.width=w;
    img.height=h;
    img.channels=c;
    img.data.assign(data,data+(w*h*c));
    int rowSize=w*c;
    for(int y=0;y<h/2;++y) {
        unsigned char* top=img.data.data()+y*rowSize;
        unsigned char* bot=img.data.data()+(h-1-y)*rowSize;
        for(int x=0;x<rowSize;++x) 
            std::swap(top[x],bot[x]);
    } SOIL_free_image_data(data);
    enqueuePendingUpload(std::move(img));
    return true;
}
