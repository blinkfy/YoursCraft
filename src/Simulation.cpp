#include "../include/Simulation.h"
#include "../include/Common.h"
#include <glm/gtc/constants.hpp>
#include <iostream>

namespace Simulation {

static std::vector<Sphere> s_spheres;
static GLuint s_sphereVAO=0,s_sphereVBO=0,s_sphereEBO=0;
static int s_sphereIndexCount=0;

void initSphereMesh(int lat,int lon,float radius) {
    //创建UV球网格
    std::vector<float> verts;
    verts.reserve((lat+1)*(lon+1)*6);
    std::vector<unsigned int> inds;
    inds.reserve(lat*lon*6);
    for(int y=0;y<=lat;++y) {
        float v=(float)y/(float)lat;
        float theta=v*glm::pi<float>();
        for(int x=0;x<=lon;++x) {
            float u=(float)x/(float)lon;
            float phi=u*glm::two_pi<float>();
            float nx=sinf(theta)*cosf(phi);
            float ny=cosf(theta);
            float nz=sinf(theta)*sinf(phi);
            //位置
            verts.push_back(nx*radius);
            verts.push_back(ny*radius);
            verts.push_back(nz*radius);
            //纹理坐标
            verts.push_back(u);
            verts.push_back(1.0f-v);
            //法线
            verts.push_back(nx);
            verts.push_back(ny);
            verts.push_back(nz);
        }
    }
    for(int y=0;y<lat;++y) {
        for(int x=0;x<lon;++x) {
            int i0=y*(lon+1)+x;
            int i1=i0+1;
            int i2=i0+(lon+1);
            int i3=i2+1;
            inds.push_back(i0);
            inds.push_back(i2);
            inds.push_back(i1);
            inds.push_back(i1);
            inds.push_back(i2);
            inds.push_back(i3);
        }
    }
    s_sphereIndexCount=(int)inds.size();
    glGenVertexArrays(1,&s_sphereVAO);
    glGenBuffers(1,&s_sphereVBO);
    glGenBuffers(1,&s_sphereEBO);
    glBindVertexArray(s_sphereVAO);
    glBindBuffer(GL_ARRAY_BUFFER,s_sphereVBO);
    glBufferData(GL_ARRAY_BUFFER,verts.size()*sizeof(float),verts.data(),GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,s_sphereEBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,inds.size()*sizeof(unsigned int),inds.data(),GL_STATIC_DRAW);
    int stride=8*sizeof(float);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,stride,(void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,stride,(void*)(3*sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2,3,GL_FLOAT,GL_FALSE,stride,(void*)(5*sizeof(float)));
    glBindVertexArray(0);
}

void spawnSphereAt(const glm::vec3& pos,float radius,int texIndex) {
    Sphere s;
    s.pos=pos;
    s.vel=glm::vec3(0.0f);
    s.radius=radius;
    s.active=true;
    s.texIndex=texIndex;
    s_spheres.push_back(s);
}

void applyPlayerPush(const Camera& camera) {
    static glm::vec3 prevCameraPos=glm::vec3(0.0f);
    glm::vec3 moveVec=camera.position-prevCameraPos;
    float moveLen=glm::length(moveVec);
    if(moveLen>0.0001f) {
        glm::vec3 pushDir=glm::normalize(moveVec);
        glm::vec3 foot=camera.getFootPosition();
        const float pushRadius=1.0f;
        const float pushStrength=3.0f;
        for(auto &s : s_spheres) {
            if(!s.active) continue;
            float dist=glm::length(s.pos-foot);
            if(dist<=pushRadius+s.radius) {
                float factor=1.0f-glm::clamp((dist-s.radius)/pushRadius,0.0f,1.0f);
                float added=glm::clamp(moveLen*pushStrength*factor,0.0f,12.0f);
                glm::vec3 hor=glm::normalize(glm::vec3(pushDir.x,0.0f,pushDir.z));
                if(glm::length(hor)<0.0001f) hor=pushDir;
                s.vel += hor*added;
            }
        }
    }
    prevCameraPos=camera.position;
}

void updateSpheres(float deltaTime,World& world) {
    const glm::vec3 gravity=glm::vec3(0.0f,-9.8f,0.0f);
    for(auto &s : s_spheres) {
        if(!s.active) continue;
        s.vel += gravity*deltaTime;
        glm::vec3 displacement=s.vel*deltaTime;
        s.pos += displacement;
        if(s.radius>0.0f) {
            //旋转与纹理偏移（基于水平位移）
            glm::vec3 dh=glm::vec3(displacement.x,0.0f,displacement.z);
            float len=glm::length(dh);
            if(len>1e-6f) {
                glm::vec3 dir=glm::normalize(dh);
                glm::vec3 up=glm::vec3(0.0f,1.0f,0.0f);
                glm::vec3 axis=glm::cross(up,dir);
                float axisLen=glm::length(axis);
                if(axisLen>1e-6f) {
                    axis=axis/axisLen;
                    float angle=len/s.radius;
                    glm::quat q=glm::angleAxis(-angle,axis);
                    s.orientation=glm::normalize(q*s.orientation);
                    const float circ=2.0f*glm::pi<float>()*s.radius;
                    glm::vec2 d2=glm::vec2(-displacement.x,-displacement.z);
                    s.texOffset += d2/circ;
                }
            }
        }
        int minX=(int)floor(s.pos.x-s.radius);
        int maxX=(int)floor(s.pos.x+s.radius);
        int minY=(int)floor(s.pos.y-s.radius);
        int maxY=(int)floor(s.pos.y+s.radius);
        int minZ=(int)floor(s.pos.z-s.radius);
        int maxZ=(int)floor(s.pos.z+s.radius);
        for(int bx=minX;bx<=maxX;++bx) {
            for(int by=minY;by<=maxY;++by) {
                for(int bz=minZ;bz<=maxZ;++bz) {
                    if(by<0) continue;
                    BlockType bt=world.getBlock(bx,by,bz);
                    if(bt==AIR) continue;
                    float closestX=glm::clamp(s.pos.x,(float)bx,(float)(bx+1));
                    float closestY=glm::clamp(s.pos.y,(float)by,(float)(by+1));
                    float closestZ=glm::clamp(s.pos.z,(float)bz,(float)(bz+1));
                    glm::vec3 closestPoint=glm::vec3(closestX,closestY,closestZ);
                    glm::vec3 diff=s.pos-closestPoint;
                    float dist2=glm::dot(diff,diff);
                    float r=s.radius;
                    if(dist2<r*r && dist2>0.000001f) {
                        float dist=sqrtf(dist2);
                        float penetration=r-dist;
                        glm::vec3 n=diff/dist;
                        s.pos += n*penetration+n*0.001f;
                        float vn=glm::dot(s.vel,n);
                        if(vn<0.0f) s.vel -= n*(vn*1.25f);
                    } else if(dist2<=0.000001f) {
                        glm::vec3 n=glm::vec3(0.0f,1.0f,0.0f);
                        s.pos += n*(r+0.001f);
                        float vn=glm::dot(s.vel,n);
                        if(vn<0.0f) s.vel -= n*(vn*1.25f);
                    }
                }
            }
        }
        if(s.pos.y-s.radius<0.0f) {
            s.pos.y=s.radius;
            if(s.vel.y<0.0f) s.vel.y *= -0.25f;
            if(fabs(s.vel.y)<0.5f) s.vel.y=0.0f;
        }
    }
    const float restitution=0.35f;
    for(size_t i=0;i<s_spheres.size();++i) {
        for(size_t j=i+1;j<s_spheres.size();++j) {
            Sphere &a=s_spheres[i];
            Sphere &b=s_spheres[j];
            if(!a.active || !b.active) continue;
            glm::vec3 diff=b.pos-a.pos;
            float dist2=glm::dot(diff,diff);
            float rsum=a.radius+b.radius;
            float rsum2=rsum*rsum;
            if(dist2<=0.000001f) {
                diff=glm::vec3(0.01f,0.01f,0.0f);
                dist2=glm::dot(diff,diff);
            }
            if(dist2<rsum2) {
                float dist=sqrtf(dist2);
                glm::vec3 n=diff/dist;
                float overlap=rsum-dist;
                glm::vec3 correction=n*(overlap*0.5f+0.001f);
                a.pos -= correction;
                b.pos += correction;
                glm::vec3 rv=b.vel-a.vel;
                float velAlongN=glm::dot(rv,n);
                if(velAlongN<0.0f) {
                    float j=-(1.0f+restitution)*velAlongN*0.5f;
                    glm::vec3 impulse=j*n;
                    a.vel -= impulse;
                    b.vel += impulse;
                    glm::vec3 tang=rv-velAlongN*n;
                    float tangLen=glm::length(tang);
                    if(tangLen>1e-6f) {
                        glm::vec3 tdir=tang/tangLen;
                        float vt=glm::dot(rv,tdir);
                        const float frictionScale=0.25f;
                        float jt=-vt*0.5f*frictionScale;
                        glm::vec3 fimp=jt*tdir;
                        a.vel -= fimp;
                        b.vel += fimp;
                    }
                }
            }
        }
    }
}

void renderSpheres(Shader& shader,GLuint sphereTexture) {
    shader.use();
    for(const auto &s : s_spheres) {
        if(!s.active) continue;
        glm::mat4 rot4=glm::mat4_cast(s.orientation);
        glm::mat4 m=glm::translate(glm::mat4(1.0f),s.pos)*rot4*glm::scale(glm::mat4(1.0f),glm::vec3(s.radius));
        shader.setMat4("model",m);
        glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,sphereTexture);
        shader.setInt("useVertexUVDeform",1);
        glm::mat3 rotMat=glm::mat3_cast(s.orientation);
        shader.setMat3("deformRot",rotMat);
        shader.setFloat("deformRadius",s.radius);
        glBindVertexArray(s_sphereVAO);
        glDrawElements(GL_TRIANGLES,s_sphereIndexCount,GL_UNSIGNED_INT,0);
        glBindVertexArray(0);
        shader.setInt("useVertexUVDeform",0);
    }
}

void renderSpheresDepth(Shader& depthShader) {
    depthShader.use();
    for(const auto &s : s_spheres) {
        if(!s.active) continue;
        glm::mat4 rot4=glm::mat4_cast(s.orientation);
        glm::mat4 modelSphere=glm::translate(glm::mat4(1.0f),s.pos)*rot4*glm::scale(glm::mat4(1.0f),glm::vec3(s.radius));
        depthShader.setMat4("model",modelSphere);
        glBindVertexArray(s_sphereVAO);
        glDrawElements(GL_TRIANGLES,s_sphereIndexCount,GL_UNSIGNED_INT,0);
        glBindVertexArray(0);
    }
}

void clearSpheres() {
    s_spheres.clear();
}

} //namespace Simulation
