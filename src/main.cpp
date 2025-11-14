#include "../include/Common.h"
#include "../include/Shader.h"
#include "../include/Texture.h"
#include "../include/World.h"
#include "../include/Simulation.h"
#include <chrono>
#include <functional>
#include <iostream>
#include <vector>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>

World world;
Camera camera;
float deltaTime=0.0f;
float lastFrame=0.0f;
bool firstMouse=true;
float lastX=WINDOW_WIDTH/2.0f;
float lastY=WINDOW_HEIGHT/2.0f;
bool keys[1024]={ false };

//选中放置的方块类型，默认 STONE
BlockType g_selectedBlockType=STONE;

int screenWidth=WINDOW_WIDTH;
int screenHeight=WINDOW_HEIGHT;
const unsigned int SHADOW_WIDTH=2048,SHADOW_HEIGHT=2048;

//昼夜周期参数
float g_timeOfDay=250.0f;//秒
const float DAY_LENGTH_SECONDS=300.0f;//一天时长

//鼠标锁定状态：true=锁定(视角控制)，false=可见光标
static bool g_cursorLocked=true;

static const char* blockTypeToName(BlockType t){
    switch(t){
    case AIR: return "AIR";
    case GRASS: return "GRASS";
    case DIRT: return "DIRT";
    case STONE: return "STONE";
    case WOOD: return "WOOD";
    case LEAVES: return "LEAVES";
    case SAND: return "SAND";
    case WATER: return "WATER";
    default: return "UNKNOWN";
    }
}

void mouseCallback(GLFWwindow* window,double xpos,double ypos){
    if(!g_cursorLocked) return;//解锁时忽略鼠标移动
    if(firstMouse){ lastX=(float)xpos;lastY=(float)ypos;firstMouse=false;}
    float xoffset=(float)xpos-lastX;
    float yoffset=lastY-(float)ypos;
    lastX=(float)xpos;lastY=(float)ypos;
    camera.processMouseMovement(xoffset,yoffset);
}

void framebufferSizeCallback(GLFWwindow* window,int width,int height){
    if(width<=0||height<=0) return;
    screenWidth=width;screenHeight=height;
    glViewport(0,0,width,height);
    lastX=width/2.0f;lastY=height/2.0f;
}

void keyCallback(GLFWwindow* window,int key,int scancode,int action,int mods){
    if(action==GLFW_PRESS){
        keys[key]=true;
        if(key==GLFW_KEY_ESCAPE) glfwSetWindowShouldClose(window,true);

        //TAB 切换鼠标锁定
        if(key==GLFW_KEY_TAB){
            g_cursorLocked=!g_cursorLocked;
            glfwSetInputMode(window,GLFW_CURSOR,g_cursorLocked?GLFW_CURSOR_DISABLED:GLFW_CURSOR_NORMAL);
            firstMouse=true;//重新锁定时避免跳变
            std::cout<<"Cursor locked: "<<(g_cursorLocked?"YES":"NO")<<std::endl;
            return;
        }

        //数字键选择方块
        if(key==GLFW_KEY_1){ 
            g_selectedBlockType=GRASS;     
            std::cout<<"Selected block: "<<blockTypeToName(g_selectedBlockType)<<std::endl;
        }else if(key==GLFW_KEY_2){ 
            g_selectedBlockType=DIRT;
            std::cout<<"Selected block: "<<blockTypeToName(g_selectedBlockType)<<std::endl;
        }else if(key==GLFW_KEY_3){
            g_selectedBlockType=STONE;
            std::cout<<"Selected block: "<<blockTypeToName(g_selectedBlockType)<<std::endl;
        }else if(key==GLFW_KEY_4){ 
            g_selectedBlockType=WOOD; 
            std::cout<<"Selected block: "<<blockTypeToName(g_selectedBlockType)<<std::endl;
        }else if(key==GLFW_KEY_5){ 
            g_selectedBlockType=LEAVES;
            std::cout<<"Selected block: "<<blockTypeToName(g_selectedBlockType)<<std::endl;
        }else if(key==GLFW_KEY_6){ 
            g_selectedBlockType=SAND; 
            std::cout<<"Selected block: "<<blockTypeToName(g_selectedBlockType)<<std::endl;
        }else if(key==GLFW_KEY_7){ 
            g_selectedBlockType=WATER;
            std::cout<<"Selected block: "<<blockTypeToName(g_selectedBlockType)<<std::endl;
        }else if(key==GLFW_KEY_B){//B：在目标方块处生成自由球体
            glm::vec3 rayStart=camera.position;glm::vec3 rayDir=camera.front;
            for(float t=0.0f;t<REACH_DISTANCE;t+=0.1f){
                glm::vec3 p=rayStart+rayDir*t;int bx=(int)floor(p.x);int by=(int)floor(p.y);int bz=(int)floor(p.z);
                if(world.getBlock(bx,by,bz)!=AIR){
                    glm::vec3 local=p-glm::vec3((float)bx,(float)by,(float)bz);
                    float dx=fmin(local.x,1.0f-local.x);
                    float dy=fmin(local.y,1.0f-local.y);
                    float dz=fmin(local.z,1.0f-local.z);
                    int sbx=bx,sby=by,sbz=bz;
                    if(dx<=dy&&dx<=dz){ if(local.x<0.5f) sbx=bx-1;else sbx=bx+1;}
                    else if(dy<=dx&&dy<=dz){ if(local.y<0.5f) sby=by-1;else sby=by+1;}
                    else{ if(local.z<0.5f) sbz=bz-1;else sbz=bz+1;}
                    int texIdxForSphere=(g_selectedBlockType==GRASS)?0:blockTypeToTexIndex(g_selectedBlockType);
                    glm::vec3 spawnPos;
                    if(sby>=0&&world.getBlock(sbx,sby,sbz)==AIR){
                        spawnPos=glm::vec3((float)sbx+0.5f,(float)sby+0.5f,(float)sbz+0.5f);
                        if(sby==by+1) spawnPos.y=(float)(sby)+0.4f+0.01f;
                    }else{
                        spawnPos=glm::vec3((float)bx+0.5f,(float)(by+1.0f)+0.4f+0.01f,(float)bz+0.5f);
                    }
                    Simulation::spawnSphereAt(spawnPos,0.4f,texIdxForSphere);
                    std::cout<<"Spawned sphere at "<<(int)floor(spawnPos.x)<<","<<(int)floor(spawnPos.y)<<","<<(int)floor(spawnPos.z)<<std::endl;
                    break;
                }
            }
        }
    }else if(key==GLFW_KEY_M){
        camera.toggleMovementMode();
        std::cout<<"运动模式: "<<(camera.getMovementMode()==Camera::GRAVITY_MODE?"重力":"飞行")<<std::endl;
    }else if(action==GLFW_RELEASE) keys[key]=false;
}

//HUD 顺序和布局
static BlockType hudBlockOrder[]={ GRASS,DIRT,STONE,WOOD,LEAVES,SAND,WATER };
static int hudTileSizePx=32;//每个格子大小（像素）
static int hudTileSpacingPx=6;//间隔
static int hudTileMarginLeftPx=10;
static int hudTileMarginBottomPx=10;

//滚轮切换选中方块
void scrollCallback(GLFWwindow* window,double xoffset,double yoffset){
    int total=(int)(sizeof(hudBlockOrder)/sizeof(hudBlockOrder[0]));
    int idx=0;
    for(int i=0;i<total;++i){
        if(hudBlockOrder[i]==g_selectedBlockType){ 
            idx=i;
            break;
        }
    }
    if(yoffset>0.0) 
        idx=(idx-1+total)%total;
    else if(yoffset<0.0)
        idx=(idx+1)%total;
    g_selectedBlockType=hudBlockOrder[idx];
    std::cout<<"Selected block: "<<blockTypeToName(g_selectedBlockType)<<std::endl;
}

void mouseButtonCallback(GLFWwindow* window,int button,int action,int mods){
    if(action!=GLFW_PRESS) return;
    bool cameraUnderwater=false;
    int cbx=(int)floor(camera.position.x);
    int cby=(int)floor(camera.position.y);
    int cbz=(int)floor(camera.position.z);
    if(cby>=0){
        try{ 
            cameraUnderwater=(world.getBlock(cbx,cby,cbz)==WATER);
        }catch(...){ 
            cameraUnderwater=false;
        }
    }

    glm::vec3 rayStart=camera.position;
    glm::vec3 rayDir=camera.front;
    for(float t=0.0f;t<REACH_DISTANCE;t+=0.1f){
        glm::vec3 pos=rayStart+rayDir*t;
        int x=(int)floor(pos.x);
        int y=(int)floor(pos.y);
        int z=(int)floor(pos.z);

        BlockType bt=world.getBlock(x,y,z);
        if(cameraUnderwater&&bt==WATER) continue;//水下视角把水当透明

        if(bt!=AIR){
            if(button==GLFW_MOUSE_BUTTON_LEFT){
                //挖掉目标方块
                world.setBlock(x,y,z,AIR);
            }else if(button==GLFW_MOUSE_BUTTON_RIGHT){
                //放到击中点前一个格子
                glm::vec3 prevPos=rayStart+rayDir*(t-0.1f);
                int px=(int)floor(prevPos.x);
                int py=(int)floor(prevPos.y);
                int pz=(int)floor(prevPos.z);
                world.setBlock(px,py,pz,g_selectedBlockType);
            }
            break;
        }
    }
}

void processInput(GLFWwindow* window){
    if(keys[GLFW_KEY_W]) camera.processKeyboard(0,deltaTime,world);
    if(keys[GLFW_KEY_S]) camera.processKeyboard(1,deltaTime,world);
    if(keys[GLFW_KEY_A]) camera.processKeyboard(2,deltaTime,world);
    if(keys[GLFW_KEY_D]) camera.processKeyboard(3,deltaTime,world);
    if(keys[GLFW_KEY_Z]) camera.processKeyboard(4,deltaTime,world);
    if(keys[GLFW_KEY_X]) camera.processKeyboard(5,deltaTime,world);
}

//初始 quad 数据（每次绘制会被更新）
static float quadVertices[]={
    -1.0f,1.0f,0.0f,1.0f,
    -1.0f,-1.0f,0.0f,0.0f,
    1.0f,-1.0f,1.0f,0.0f,

    -1.0f,1.0f,0.0f,1.0f,
    1.0f,-1.0f,1.0f,0.0f,
    1.0f,1.0f,1.0f,1.0f
};

int main(){
    if(!glfwInit()) return -1;
    std::cout<<"初始化中……"<<std::endl;
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,3);
    glfwWindowHint(GLFW_OPENGL_PROFILE,GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_RESIZABLE,GLFW_TRUE);

    GLFWwindow* window=glfwCreateWindow(WINDOW_WIDTH,WINDOW_HEIGHT,"Yourscraft",NULL,NULL);
    if(!window){ 
        glfwTerminate();
        return -1;
    }

    glfwMakeContextCurrent(window);
    glfwSetCursorPosCallback(window,mouseCallback);
    glfwSetKeyCallback(window,keyCallback);
    glfwSetMouseButtonCallback(window,mouseButtonCallback);
    glfwSetFramebufferSizeCallback(window,framebufferSizeCallback);
    glfwSetScrollCallback(window,scrollCallback);
    glfwSetInputMode(window,GLFW_CURSOR,g_cursorLocked?GLFW_CURSOR_DISABLED:GLFW_CURSOR_NORMAL);
    glfwSwapInterval(1);
    glfwShowWindow(window);
    glfwFocusWindow(window);
    glfwPollEvents();

    if(!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)){ 
        std::cout<<"GLAD init failed"<<std::endl;
        return -1;
    }
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
    glViewport(0,0,WINDOW_WIDTH,WINDOW_HEIGHT);

    Shader splashShader;
    const char* splashVert=R"(
        #version 330 core
        layout(location=0) in vec2 aPos;
        layout(location=1) in vec2 aTex;
        out vec2 TexCoord;
        void main(){ TexCoord=aTex;gl_Position=vec4(aPos,0.0,1.0);}
    )";
    const char* splashFrag=R"(
        #version 330 core
        in vec2 TexCoord;out vec4 FragColor;
        uniform sampler2D splashTex;
        void main(){ FragColor=texture(splashTex,TexCoord);}
    )";
    splashShader.compile(splashVert,splashFrag);

    unsigned int quadVAO,quadVBO;
    glGenVertexArrays(1,&quadVAO);
    glGenBuffers(1,&quadVBO);
    glBindVertexArray(quadVAO);
    glBindBuffer(GL_ARRAY_BUFFER,quadVBO);
    glBufferData(GL_ARRAY_BUFFER,sizeof(quadVertices),quadVertices,GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,4*sizeof(float),(void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,4*sizeof(float),(void*)(2*sizeof(float)));
    glBindVertexArray(0);

    Shader shader,depthShader;
    unsigned int depthMapFBO=0,depthMap=0;
    startTextureLoader();
    Simulation::initSphereMesh(14,14,1.0f);

    std::vector<std::function<void()>> initTasks;
    initTasks.push_back([&](){initPanoramaTextureHandles();});
    for(int i=0;i<6;++i) 
        initTasks.push_back([&,i](){ requestPanoramaLoad(i);});
    initTasks.push_back([&](){ initBlockTextureHandles();});
    for(int i=0;i<NUM_BLOCK_TEXTURES;++i) 
        initTasks.push_back([&,i](){ requestBlockTextureLoad(i);});

    initTasks.push_back([&](){
        const char* vsrc=R"(
            #version 330 core
            layout (location=0) in vec3 aPos;
            layout (location=1) in vec2 aTexCoord;
            layout (location=2) in vec3 aNormal;
            out vec3 FragPos;
            out vec2 TexCoord;
            out vec3 Normal;
            out vec4 FragPosLightSpace;
            uniform mat4 model;
            uniform mat4 view;
            uniform mat4 projection;
            uniform mat4 lightSpaceMatrix;
            uniform int useVertexUVDeform;
            uniform mat3 deformRot;
            uniform float deformRadius;
            const float PI=3.14159265359;
            void main() {
                vec3 p=aPos;
                vec2 outUV=aTexCoord;
                vec3 n=aNormal;
                if(useVertexUVDeform==1) {
                    vec3 p_rot=transpose(deformRot)*p;
                    float lon=atan(p_rot.z,p_rot.x);
                    float lat=acos(clamp(p_rot.y/length(p_rot),-1.0,1.0));
                    float u=(lon/(2.0*PI))+0.5;
                    float v=1.0-(lat/PI);
                    outUV=vec2(u,v);
                }
                TexCoord=outUV;
                vec4 worldPos=model*vec4(p,1.0);
                FragPos=worldPos.xyz;
                gl_Position=projection*view*worldPos;
                Normal=mat3(transpose(inverse(model)))*n;
                FragPosLightSpace=lightSpaceMatrix*worldPos;
            }
        )";
        const char* fsrc=R"(
            #version 330 core
            in vec3 FragPos;
            in vec2 TexCoord;
            in vec3 Normal;
            in vec4 FragPosLightSpace;
            out vec4 FragColor;
            uniform vec3 viewPos;
            uniform vec3 lightPos;
            uniform vec3 lightColor;
            uniform sampler2D texture1;
            uniform sampler2D shadowMap;
            uniform float shininess;
            uniform float specularStrength;
            uniform float ambientStrength;
            uniform vec3 fogColor;
            uniform float fogNear;
            uniform float fogFar;
            uniform float cloudShadowFactor;
            float ShadowCalculation(vec4 fragPosLightSpace) {
                vec3 projCoords=fragPosLightSpace.xyz/fragPosLightSpace.w;
                projCoords=projCoords*0.5+0.5;
                float closestDepth=texture(shadowMap,projCoords.xy).r;
                float currentDepth=projCoords.z;
                float bias=max(0.005*(1.0-dot(normalize(Normal),normalize(lightPos-FragPos))),0.0005);
                float shadow=0.0;
                vec2 texelSize=1.0/textureSize(shadowMap,0);
                for(int x=-1;x<=1;++x){
                    for(int y=-1;y<=1;++y) { 
                        float pcfDepth=texture(shadowMap,projCoords.xy+vec2(x,y)*texelSize).r;
                        shadow += currentDepth-bias>pcfDepth ? 1.0 : 0.0;
                    }
                }
                shadow /= 9.0;
                if(projCoords.z>1.0) shadow=0.0;
                return shadow*cloudShadowFactor;
            }
            void main(){
                vec3 ambient=ambientStrength*lightColor;
                vec3 norm=normalize(Normal);
                vec3 lightDir=normalize(lightPos-FragPos);
                float diff=max(dot(norm,lightDir),0.0);
                vec3 diffuse=diff*lightColor;
                vec3 viewDir=normalize(viewPos-FragPos);
                vec3 halfwayDir=normalize(lightDir+viewDir);
                float spec=pow(max(dot(norm,halfwayDir),0.0),shininess);
                vec3 specular=specularStrength*spec*lightColor;
                vec4 texSample=texture(texture1,TexCoord);
                vec3 texColor=texSample.rgb;
                float texAlpha=texSample.a;
                float shadow=ShadowCalculation(FragPosLightSpace);
                vec3 lighting=(ambient+(1.0-shadow)*(diffuse+specular))*texColor;
                float dist=length(viewPos-FragPos);
                float fogFactor=1.0;
                if(fogFar>fogNear) fogFactor=clamp((fogFar-dist)/(fogFar-fogNear),0.0,1.0);
                vec3 finalColor=mix(fogColor,lighting,fogFactor);
                FragColor=vec4(finalColor,texAlpha);
            }
        )";
        shader.compile(vsrc,fsrc);
    });

    initTasks.push_back([&](){
        const char* dv=R"(
            #version 330 core
            layout (location=0) in vec3 aPos;
            uniform mat4 model;uniform mat4 lightSpaceMatrix;
            void main(){ gl_Position=lightSpaceMatrix*model*vec4(aPos,1.0);}
        )";
        const char* df=R"(
            #version 330 core
            void main(){}
        )";
        depthShader.compile(dv,df);
    });

    //创建阴影帧缓冲
    initTasks.push_back([&](){
        glGenFramebuffers(1,&depthMapFBO);
        glGenTextures(1,&depthMap);
        glBindTexture(GL_TEXTURE_2D,depthMap);
        glTexImage2D(GL_TEXTURE_2D,0,GL_DEPTH_COMPONENT,SHADOW_WIDTH,SHADOW_HEIGHT,0,GL_DEPTH_COMPONENT,GL_FLOAT,NULL);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_BORDER);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_BORDER);
        float borderColor[]={1.0,1.0,1.0,1.0};
        glTexParameterfv(GL_TEXTURE_2D,GL_TEXTURE_BORDER_COLOR,borderColor);
        glBindFramebuffer(GL_FRAMEBUFFER,depthMapFBO);
        glFramebufferTexture2D(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_TEXTURE_2D,depthMap,0);
        glDrawBuffer(GL_NONE);
        glReadBuffer(GL_NONE);
        glBindFramebuffer(GL_FRAMEBUFFER,0);
    });

    initTasks.push_back([&](){ 
        shader.use();
        shader.setInt("texture1",0);
        shader.setInt("shadowMap",1);
    });
    initTasks.push_back([&](){ initTitleTextureHandle();});
    initTasks.push_back([&](){ requestTitleTextureLoad();});
    initTasks.push_back([&](){ initSubtitleTextureHandle();});
    initTasks.push_back([&](){ requestSubtitleTextureLoad();});
    initTasks.push_back([&](){ initSphereTextureHandle();});
    initTasks.push_back([&](){ requestSphereTextureLoad();});

    const float slideSpeed=60.0f;//滑动速度（像素/秒）
    float sOff=0.0f;//偏移（像素）
    const int pages=4;//页面数
    float loops=0.0f;

    enum class OverlayState{ NONE,UP,DOWN } overlayState=OverlayState::NONE;
    float ovT=0.0f;
    const float ovDur=1.0f;//垂直动画时长（秒）

    auto drawFullScreenCropped=[&](int texIndex,float xOffPx,float yOffPx){
        int w,h;glfwGetFramebufferSize(window,&w,&h);
        float ndcShiftX=2.0f*xOffPx/(float)w;
        float ndcShiftY=2.0f*yOffPx/(float)h;
        float left=-1.0f+ndcShiftX;float right=1.0f+ndcShiftX;
        float bottom=-1.0f+ndcShiftY;float top=1.0f+ndcShiftY;
        float vRange=(float)h/(float)w;if(vRange>1.0f) vRange=1.0f;
        float v0=0.5f-0.5f*vRange;float v1=v0+vRange;
        float verts[6*4]={
            left,top,0.0f,v1,
            left,bottom,0.0f,v0,
            right,bottom,1.0f,v0,
            left,top,0.0f,v1,
            right,bottom,1.0f,v0,
            right,top,1.0f,v1
        };
        glBindBuffer(GL_ARRAY_BUFFER,quadVBO);
        glBufferSubData(GL_ARRAY_BUFFER,0,sizeof(verts),verts);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D,panoramaTextures[texIndex]);
        splashShader.setInt("splashTex",0);
        glBindVertexArray(quadVAO);
        glDrawArrays(GL_TRIANGLES,0,6);
        glBindVertexArray(0);
    };

    auto drawTitleCentered=[&](float scale){
        int w,h;glfwGetFramebufferSize(window,&w,&h);
        float ndcWidth=3.6f*scale;
        float ndcHeight=1.3f*scale;
        float left=-ndcWidth/2.0f;
        float right=ndcWidth/2.0f;
        float verticalOffset=0.6f;
        float bottom=-ndcHeight/2.0f+verticalOffset;
        float top=ndcHeight/2.0f+verticalOffset;
        float verts[6*4]={
            left,top,0.0f,1.0f,
            left,bottom,0.0f,0.0f,
            right,bottom,1.0f,0.0f,
            left,top,0.0f,1.0f,
            right,bottom,1.0f,0.0f,
            right,top,1.0f,1.0f
        };
        glBindBuffer(GL_ARRAY_BUFFER,quadVBO);
        glBufferSubData(GL_ARRAY_BUFFER,0,sizeof(verts),verts);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D,titleTexture);
        splashShader.setInt("splashTex",0);
        glBindVertexArray(quadVAO);
        glDrawArrays(GL_TRIANGLES,0,6);
        glBindVertexArray(0);
    };

    auto drawSubtitleCentered=[&](float scale){
        int w,h;glfwGetFramebufferSize(window,&w,&h);
        float ndcWidth=0.8f*scale;
        float ndcHeight=0.6f*scale;
        float left=-ndcWidth/2.0f;
        float right=ndcWidth/2.0f;
        float verticalOffset=0.25f;
        float bottom=-ndcHeight/2.0f+verticalOffset;
        float top=ndcHeight/2.0f+verticalOffset;
        float verts[6*4]={
            left,top,0.0f,1.0f,
            left,bottom,0.0f,0.0f,
            right,bottom,1.0f,0.0f,
            left,top,0.0f,1.0f,
            right,bottom,1.0f,0.0f,
            right,top,1.0f,1.0f
        };
        glBindBuffer(GL_ARRAY_BUFFER,quadVBO);
        glBufferSubData(GL_ARRAY_BUFFER,0,sizeof(verts),verts);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D,subtitleTexture);
        splashShader.setInt("splashTex",0);
        glBindVertexArray(quadVAO);
        glDrawArrays(GL_TRIANGLES,0,6);
        glBindVertexArray(0);
    };

    size_t taskIndex=0;
    lastFrame=(float)glfwGetTime();

    while(taskIndex<initTasks.size()&&!glfwWindowShouldClose(window)){
        float t=(float)glfwGetTime();
        deltaTime=t-lastFrame;
        lastFrame=t;
        glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
        glDepthMask(GL_FALSE);
        splashShader.use();

        drawTitleCentered(0.4f);
        drawSubtitleCentered(0.9f);
        int wpx,hpx;
        glfwGetFramebufferSize(window,&wpx,&hpx);
        float totalWidth=(float)wpx*pages;
        sOff+=slideSpeed*deltaTime;
        if(sOff>=totalWidth){ 
            sOff=fmodf(sOff,totalWidth);
            overlayState=OverlayState::UP;
            ovT=0.0f;
        }

        for(int i=0;i<pages;++i){
            float baseX=i*(float)wpx;
            for(int copy=-1;copy<=1;++copy){
                float x=baseX-sOff+copy*totalWidth;
                if(x+wpx< -0.5f) continue;
                if(x>(float)wpx*1.5f) continue;
                drawFullScreenCropped(i,x,0.0f);
            }
        }

        if(overlayState!=OverlayState::NONE){
            ovT+=deltaTime;
            if(overlayState==OverlayState::UP){ 
                float p=ovT/ovDur;
                if(p>1.0f) p=1.0f;
                float yOff=-p*0.3f*(float)hpx;
                drawFullScreenCropped(4,0.0f,yOff);
                if(ovT>=ovDur){ 
                    overlayState=OverlayState::DOWN;
                    ovT=0.0f;
                } 
            }
            else if(overlayState==OverlayState::DOWN){ 
                float p=ovT/ovDur;
                if(p>1.0f) p=1.0f;
                float yOff=p*0.3f*(float)hpx;
                drawFullScreenCropped(5,0.0f,yOff);
                if(ovT>=ovDur){
                    overlayState=OverlayState::NONE;
                    ovT=0.0f;
                }
            }
        }

        glDepthMask(GL_TRUE);

        processPendingTextureUploads(2);
        world.processUploads(4);

        initTasks[taskIndex]();
        taskIndex++;

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    bool readyToEnterMain=false;
    int stableCount=0;
    const int stableThreshold=4;
    float spawnX=0.0f,spawnZ=0.0f;
    float spawnTerrainHeight=calculateTerrainHeight(spawnX,spawnZ);
    float spawnY=spawnTerrainHeight+5.0f;
    camera.position=glm::vec3(spawnX,spawnY,spawnZ);
    camera.front=glm::vec3(0.0f,0.0f,-1.0f);
    glm::vec3 sunDir=glm::normalize(glm::vec3(-0.3f,-1.0f,-0.5f));
    world.updateChunks(camera,-sunDir);

    while(!readyToEnterMain&&!glfwWindowShouldClose(window)){
        float t=(float)glfwGetTime();
        deltaTime=t-lastFrame;
        lastFrame=t;
        glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
        glDepthMask(GL_FALSE);
        splashShader.use();

        int wpx,hpx;
        glfwGetFramebufferSize(window,&wpx,&hpx);
        float totalWidth=(float)wpx*pages;
        sOff+=slideSpeed*deltaTime;
        if(sOff>=totalWidth){ 
            sOff=fmodf(sOff,totalWidth);
            overlayState=OverlayState::UP;
            ovT=0.0f;
        }

        for(int i=0;i<pages;++i){
            float baseX=i*(float)wpx;
            for(int copy=-1;copy<=1;++copy){
                float x=baseX-sOff+copy*totalWidth;
                if(x+wpx< -0.5f) continue;
                if(x>(float)wpx*1.5f) continue;
                drawFullScreenCropped(i,x,0.0f);
            }
        }
        if(overlayState!=OverlayState::NONE){
            ovT+=deltaTime;
            if(overlayState==OverlayState::UP){ 
                float p=ovT/ovDur;
                if(p>1.0f) p=1.0f;
                float yOff=-p*0.3f*(float)hpx;
                drawFullScreenCropped(4,0.0f,yOff);
                if(ovT>=ovDur){ 
                    overlayState=OverlayState::DOWN;
                    ovT=0.0f;
                }
            }
            else if(overlayState==OverlayState::DOWN){
                float p=ovT/ovDur;
                if(p>1.0f) p=1.0f;
                float yOff=p*0.3f*(float)hpx;
                drawFullScreenCropped(5,0.0f,yOff);
                if(ovT>=ovDur){ 
                    overlayState=OverlayState::NONE;
                    ovT=0.0f;
                } 
            }
        }

        drawTitleCentered(0.4f);
        drawSubtitleCentered(0.9f);

        glDepthMask(GL_TRUE);

        processPendingTextureUploads(32);
        world.processUploads(32);
        world.updateChunks(camera,-sunDir);

        static float logTimer=0.0f;
        logTimer+=deltaTime;
        if(logTimer>0.5f){ 
            std::cout<<"Splash: pendingTasks="<<world.getPendingTasksCount()<<" texturesPending="<<hasPendingTextureUploads()<<std::endl;
            logTimer=0.0f;
        }

        bool noTexturePending=!hasPendingTextureUploads();
        bool noWorldPending=world.getPendingTasksCount()==0;
        if(noTexturePending&&noWorldPending){ 
            stableCount++;
            if(stableCount>=stableThreshold) readyToEnterMain=true;
        }else stableCount=0;

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    glDeleteVertexArrays(1,&quadVAO);
    glDeleteBuffers(1,&quadVBO);

    unsigned int hudVAO=0,hudVBO=0;
    glGenVertexArrays(1,&hudVAO);
    glGenBuffers(1,&hudVBO);
    glBindVertexArray(hudVAO);
    glBindBuffer(GL_ARRAY_BUFFER,hudVBO);
    const int HUD_VERT_COUNT=6;
    const int HUD_FLOATS_PER_VERT=4;
    glBufferData(GL_ARRAY_BUFFER,HUD_VERT_COUNT*HUD_FLOATS_PER_VERT*sizeof(float),nullptr,GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,4*sizeof(float),(void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,4*sizeof(float),(void*)(2*sizeof(float)));
    glBindVertexArray(0);

    const int hudPixelSize=80;
    const int hudMarginPx=10;

    std::cout<<"初始化完成"<<std::endl;
    std::cout<<"AWSDZX移动\n鼠标移动视角\n左键挖掘方块\n右键放置方块\n滚轮选择方块\nTAB键切换鼠标锁定\nM切换运动模式\nB放置球体"<<std::endl;
    float spawnX2=0.0f,spawnZ2=0.0f;
    float spawnTerrainHeight2=calculateTerrainHeight(spawnX2,spawnZ2);
    float spawnY2=spawnTerrainHeight2+5.0f;
    camera.position=glm::vec3(spawnX2,spawnY2,spawnZ2);
    while(!glfwWindowShouldClose(window)){
        float currentFrame=(float)glfwGetTime();
        deltaTime=currentFrame-lastFrame;
        lastFrame=currentFrame;

        processInput(window);
        camera.updatePhysics(deltaTime,world);
        processPendingTextureUploads(4);

        Simulation::applyPlayerPush(camera);

        g_timeOfDay+=deltaTime;
        if(g_timeOfDay>=DAY_LENGTH_SECONDS) g_timeOfDay=fmodf(g_timeOfDay,DAY_LENGTH_SECONDS);
        float tt=g_timeOfDay/DAY_LENGTH_SECONDS;

        glm::vec3 sunDir=glm::normalize(glm::vec3(glm::sin(tt*2.0f*3.14159265f),glm::cos(tt*2.0f*3.14159265f),glm::sin(tt*3.14159265f*0.5f)));
        float sunHeight=glm::clamp(sunDir.y,-1.0f,1.0f);
        if(sunHeight<-0.01f) g_timeOfDay+=2*deltaTime;//夜间加速
        float dayFactor=(sunHeight+0.4f)/1.4f;
        if(dayFactor<0.0f) dayFactor=0.0f;
        else if(dayFactor>1.0f) dayFactor=1.0f;

        glm::vec3 dayLightColor=glm::vec3(1.0f,0.98f,0.9f);
        glm::vec3 nightLightColor=glm::vec3(0.3f,0.32f,0.36f);
        glm::vec3 lightColor=glm::mix(nightLightColor,dayLightColor,std::min(dayFactor,0.7f));
        float ambientStrength=0.75f;
        glm::vec3 daySky=glm::vec3(0.5f,0.7f,0.9f);
        glm::vec3 nightSky=glm::vec3(0.02f,0.03f,0.08f);
        glm::vec3 clearColor=glm::mix(nightSky,daySky,dayFactor);

        glClearColor(clearColor.r,clearColor.g,clearColor.b,1.0f);
        glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);

        Simulation::updateSpheres(deltaTime,world);

        //定期运行水模拟
        world.simulateWater(camera);

        //方向光约定
        glm::vec3 lightDir=-sunDir;
        world.updateChunks(camera,lightDir);
        glm::vec3 lightPos=camera.position+sunDir*200.0f;//将光源放在太阳方向远处

        //计算光照矩阵（正交阴影贴图）
        bool doShadow=(sunHeight>0.05f);
        float near_plane=1.0f,far_plane=300.0f;
        glm::mat4 lightProjection=glm::ortho(-150.0f,150.0f,-150.0f,150.0f,near_plane,far_plane);
        glm::mat4 lightView=glm::lookAt(lightPos,camera.position,glm::vec3(0.0f,1.0f,0.0f));
        glm::mat4 lightSpaceMatrix=lightProjection*lightView;

        if(doShadow){
            depthShader.use();
            depthShader.setMat4("lightSpaceMatrix",lightSpaceMatrix);
            depthShader.setMat4("model",glm::mat4(1.0f));
            glViewport(0,0,SHADOW_WIDTH,SHADOW_HEIGHT);
            glBindFramebuffer(GL_FRAMEBUFFER,depthMapFBO);
            glClear(GL_DEPTH_BUFFER_BIT);
            for(auto &p:world.getChunks()) p.second->renderDepth(depthShader);
            //将动态球体渲染进阴影贴图
            Simulation::renderSpheresDepth(depthShader);
            glBindFramebuffer(GL_FRAMEBUFFER,0);
        }

        int width,height;
        glfwGetFramebufferSize(window,&width,&height);
        glViewport(0,0,width,height);

        shader.use();
        glm::mat4 model=glm::mat4(1.0f);
        glm::mat4 view=camera.getViewMatrix();
        glm::mat4 projection=glm::perspective(glm::radians(45.0f),(float)width/(float)height,0.1f,200.0f);
        shader.setMat4("model",model);
        shader.setMat4("view",view);
        shader.setMat4("projection",projection);
        shader.setVec3("viewPos",camera.position);
        shader.setVec3("lightPos",lightPos);
        shader.setVec3("lightColor",lightColor);
        shader.setFloat("shininess",302.0f);
        shader.setFloat("ambientStrength",ambientStrength);
        //默认雾（空气/水面以上）
        shader.setVec3("fogColor",clearColor);
        shader.setFloat("fogNear",60.0f);
        shader.setFloat("fogFar",160.0f);
        shader.setMat4("lightSpaceMatrix",lightSpaceMatrix);

        float baseCloudFactor=0.6f;
        shader.setFloat("cloudShadowFactor",(baseCloudFactor*dayFactor));
        glActiveTexture(GL_TEXTURE1);glBindTexture(GL_TEXTURE_2D,depthMap);glActiveTexture(GL_TEXTURE0);

        //若相机在水中，禁用面剔除以便从下方看到水面（通常为单面）
        int cbx=(int)floor(camera.position.x);
        int cby=(int)floor(camera.position.y);
        int cbz=(int)floor(camera.position.z);
        bool cameraUnderwater=false;
        if(cby>=0){
            try{ 
                cameraUnderwater=(world.getBlock(cbx,cby,cbz)==WATER);
            }catch(...){ 
                cameraUnderwater=false;
            }
        }
        if(cameraUnderwater) glDisable(GL_CULL_FACE);
        //Render world (opaque+global transparent pass handled inside World::render)
        //ifunderwater,tighten fog and tint to reduce visibility
        if(cameraUnderwater){
            //蓝色水下雾
            shader.setVec3("fogColor",glm::vec3(0.02f,0.06f,0.12f));
            shader.setFloat("fogNear",1.0f);
            shader.setFloat("fogFar",30.0f);
        }
        world.render(shader,camera,lightDir);
        if(cameraUnderwater) glEnable(GL_CULL_FACE);

        //用相同shader渲染球体（简单光照）
        Simulation::renderSpheres(shader,sphereTexture);

        //HUD界面
        glDisable(GL_DEPTH_TEST);
        splashShader.use();
        int fbw,fbh;
        glfwGetFramebufferSize(window,&fbw,&fbh);
        float ndcPerPixelX=2.0f/(float)fbw;
        float ndcPerPixelY=2.0f/(float)fbh;
        int totalTiles=(int)(sizeof(hudBlockOrder)/sizeof(hudBlockOrder[0]));
        for(int i=0;i<totalTiles;++i){
            BlockType bt=hudBlockOrder[i];
            int texIndex=blockTypeToTexIndex(bt);
            if(texIndex<0||texIndex>=NUM_BLOCK_TEXTURES) continue;
            int x0px=hudTileMarginLeftPx+i*(hudTileSizePx+hudTileSpacingPx);
            int y0px=fbh-(hudTileMarginBottomPx+hudTileSizePx);
            int x1px=x0px+hudTileSizePx;
            int y1px=y0px+hudTileSizePx;
            float leftN=-1.0f+x0px*ndcPerPixelX;
            float rightN=-1.0f+x1px*ndcPerPixelX;
            float topN=1.0f-y0px*ndcPerPixelY;
            float bottomN=1.0f-y1px*ndcPerPixelY;
            float hudVertsDynamic[6*4]={ leftN,topN,0.0f,1.0f,leftN,bottomN,0.0f,0.0f,rightN,bottomN,1.0f,0.0f,leftN,topN,0.0f,1.0f,rightN,bottomN,1.0f,0.0f,rightN,topN,1.0f,1.0f };
            glBindBuffer(GL_ARRAY_BUFFER,hudVBO);
            glBufferSubData(GL_ARRAY_BUFFER,0,sizeof(hudVertsDynamic),hudVertsDynamic);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D,blockTextures[texIndex]);
            splashShader.setInt("splashTex",0);
            glBindVertexArray(hudVAO);
            glEnable(GL_BLEND);
            glDrawArrays(GL_TRIANGLES,0,6);
            glDisable(GL_BLEND);
            if(bt==g_selectedBlockType){
                float borderPadPx=3.0f;
                float l2=-1.0f+(x0px-borderPadPx)*ndcPerPixelX;
                float r2=-1.0f+(x1px+borderPadPx)*ndcPerPixelX;
                float t2=1.0f-(y0px-borderPadPx)*ndcPerPixelY;
                float b2=1.0f-(y1px+borderPadPx)*ndcPerPixelY;
                float borderVerts[6*4]={ l2,t2,0.0f,1.0f,l2,b2,0.0f,0.0f,r2,b2,1.0f,0.0f,l2,t2,0.0f,1.0f,r2,b2,1.0f,0.0f,r2,t2,1.0f,1.0f };
                glBufferSubData(GL_ARRAY_BUFFER,0,sizeof(borderVerts),borderVerts);
                glEnable(GL_BLEND);
                glDrawArrays(GL_TRIANGLES,0,6);
                glDisable(GL_BLEND);
            }
        }
        glBindVertexArray(0);
        glEnable(GL_DEPTH_TEST);

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    glDeleteVertexArrays(1,&hudVAO);
    glDeleteBuffers(1,&hudVBO);

    glfwTerminate();return 0;
}