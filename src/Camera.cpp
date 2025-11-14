#include "../include/Common.h"
#include "../include/World.h"

Camera::Camera(glm::vec3 pos) : position(pos), yaw(-90.0f), pitch(0.0f), isStepping(false), stepElapsed(0.0f), stepStartY(0.0f), stepTargetDeltaY(0.0f), stepPendX(0.0f), stepPendZ(0.0f), movementMode(GRAVITY_MODE), verticalVelocity(0.0f) {
    up=glm::vec3(0.0f,1.0f,0.0f);
    updateCameraVectors();
}

glm::mat4 Camera::getViewMatrix() const { 
    return glm::lookAt(position, position+front, up);
}
glm::vec3 Camera::getFootPosition() const { 
    return position-glm::vec3(0.0f, PLAYER_EYE_HEIGHT, 0.0f);
}

bool Camera::checkCollision(const glm::vec3& pos, World& world) const {
    float halfWidth=PLAYER_WIDTH/2.0f;
    const float eps=1e-3f;
    glm::vec3 minBox=pos+glm::vec3(-halfWidth+eps, 0.0f+eps, -halfWidth+eps);
    glm::vec3 maxBox=pos+glm::vec3(halfWidth-eps, PLAYER_HEIGHT-eps, halfWidth-eps);
    int minX=(int)floor(minBox.x);
    int minY=(int)floor(minBox.y);
    int minZ=(int)floor(minBox.z);
    int maxX=(int)floor(maxBox.x);
    int maxY=(int)floor(maxBox.y);
    int maxZ=(int)floor(maxBox.z);
    for(int x=minX;x<=maxX;++x) 
        for(int y=minY;y<=maxY;++y) 
            for(int z=minZ;z<=maxZ;++z) {
                BlockType b=world.getBlock(x,y,z);
                //允许水和树叶重叠：仅把实心方块（非 AIR、非 WATER、非 LEAVES）视为碰撞
                if(b!=AIR && b!=WATER && b!=LEAVES) return true;
            }
    return false;
}

void Camera::updateCameraVectors(){ 
    glm::vec3 newFront;
    newFront.x=cos(glm::radians(yaw))*cos(glm::radians(pitch));
    newFront.y=sin(glm::radians(pitch));
    newFront.z=sin(glm::radians(yaw))*cos(glm::radians(pitch));
    front=glm::normalize(newFront);
    right=glm::normalize(glm::cross(front, glm::vec3(0.0f,1.0f,0.0f)));
    up=glm::normalize(glm::cross(right, front));
}

void Camera::processMouseMovement(float xoffset, float yoffset) {
    xoffset *= MOUSE_SENSITIVITY;
    yoffset *= MOUSE_SENSITIVITY;

    yaw += xoffset;
    pitch += yoffset;

    if(pitch>89.0f) pitch=89.0f;
    if(pitch<-89.0f) pitch=-89.0f;

    updateCameraVectors();
}

void Camera::processKeyboard(int direction, float deltaTime, World& world) {
    float baseVelocity=PLAYER_SPEED*deltaTime;
    float velocity=baseVelocity*(isStepping ? STEPPING_SPEED_MULT : 1.0f);
    const float JUMP_IMPULSE=5.0f;//跳跃时的初始向上速度
    if(isStepping) {
        stepElapsed += deltaTime;
        float t=glm::clamp(stepElapsed/STEP_DURATION, 0.0f, 1.0f);
        float newY=stepStartY+stepTargetDeltaY*t;
        float deltaY=newY-position.y;
        if(deltaY!=0.0f) {
            glm::vec3 footPos=getFootPosition();
            glm::vec3 testPos(footPos.x, footPos.y+deltaY, footPos.z);
            if(!checkCollision(glm::vec3(testPos.x, footPos.y, testPos.z), world)) position.y=newY;
            else { 
                isStepping=false;
                stepPendX=0.0f;
                stepPendZ=0.0f;
            }
        }
        if(stepPendX!=0.0f || stepPendZ!=0.0f) {
            auto canMoveCombined=[&](float dx, float dz)->bool{ glm::vec3 fp=getFootPosition();glm::vec3 tp=fp;tp.x+=dx;tp.z+=dz;return !checkCollision(glm::vec3(tp.x, fp.y, tp.z), world);};
            auto tryMoveX=[&](float dx)->bool{ 
                if(dx==0.0f) return false;
                glm::vec3 fp=getFootPosition();
                glm::vec3 tp=fp;
                tp.x+=dx;
                if(!checkCollision(glm::vec3(tp.x, fp.y, fp.z), world)){
                    position.x+=dx;
                    return true;
                } return false;
            };
            auto tryMoveZ=[&](float dz)->bool{ 
                if(dz==0.0f) return false;
                glm::vec3 fp=getFootPosition();
                glm::vec3 tp=fp;
                tp.z+=dz;
                if(!checkCollision(glm::vec3(fp.x, fp.y, tp.z), world)){ 
                    position.z+=dz;
                    return true;
                } return false;
            };
            if(canMoveCombined(stepPendX, stepPendZ)) { 
                position.x += stepPendX;
                position.z += stepPendZ;
                stepPendX=0.0f;
                stepPendZ=0.0f;
            }else{
                bool xFirst=std::fabs(stepPendX)>=std::fabs(stepPendZ);
                if(xFirst) { 
                    if(tryMoveX(stepPendX)) stepPendX=0.0f;
                    if(tryMoveZ(stepPendZ)) stepPendZ=0.0f;
                }else{ 
                    if(tryMoveZ(stepPendZ)) stepPendZ=0.0f;
                    if(tryMoveX(stepPendX)) stepPendX=0.0f;
                }
            }
        }
        if(glm::clamp(stepElapsed/STEP_DURATION, 0.0f, 1.0f)>=1.0f) isStepping=false;
    }

    glm::vec3 newPosition=position;
    glm::vec3 frontH=glm::normalize(glm::vec3(front.x, 0.0f, front.z));
    glm::vec3 rightH=glm::normalize(glm::vec3(right.x, 0.0f, right.z));
    if(direction==0) 
        newPosition += frontH*velocity;
    if(direction==1) newPosition -= frontH*velocity;
    if(direction==2) newPosition -= rightH*velocity;
    if(direction==3) newPosition += rightH*velocity;
    if(movementMode==FLY_MODE) {
        if(direction==4) newPosition += up*baseVelocity;
        if(direction==5) newPosition -= up*baseVelocity;
    } else { //重力模式
        //使用方向 4 (Z) 跳跃；方向 5 (X) 在重力模式下无效
        if(direction==4) {
            //检查玩家是否在地面：略微向下看是否有实心方块
            glm::vec3 foot=getFootPosition();
            int fx=(int)floor(foot.x);
            int fy=(int)floor(foot.y-0.05f);
            int fz=(int)floor(foot.z);
            bool onGround=false;
            for(int xx=fx-0;xx<=fx+0;++xx) {
                for(int zz=fz-0;zz<=fz+0;++zz) {
                    BlockType b=world.getBlock(xx, fy, zz);
                    if(b!=AIR && b!=WATER && b!=LEAVES) { 
                        onGround=true;
                        break;
                    }
                }
                if(onGround) break;
            }
            if(onGround) {
                //设置垂直速度以开始跳跃
                verticalVelocity=JUMP_IMPULSE;
            }
        }
        else if(direction==5){
            verticalVelocity =-JUMP_IMPULSE;
        }
    }
    glm::vec3 footPos=getFootPosition();
    float dx=newPosition.x-position.x;
    if(dx!=0.0f) { 
        glm::vec3 testPos=footPos;testPos.x=footPos.x+dx;
        if(!checkCollision(glm::vec3(testPos.x, footPos.y, testPos.z), world)) 
            position.x=newPosition.x;
        else { 
            if(!isStepping) { 
                glm::vec3 stepTestPos(testPos.x, footPos.y+STEP_HEIGHT, footPos.z);
                if(!checkCollision(stepTestPos, world)) { 
                    isStepping=true;
                    stepElapsed=0.0f;
                    stepStartY=position.y;
                    stepTargetDeltaY=STEP_HEIGHT;
                    stepPendX=dx;
                } 
            } else { 
                stepPendX += dx;
            } 
        } 
        footPos=getFootPosition();
    }
    if(direction==4||direction==5) { 
        float dy=newPosition.y-position.y;
        if(dy!=0.0f) { 
            glm::vec3 testPos=footPos;
            testPos.y=footPos.y+dy;
            if(!checkCollision(glm::vec3(footPos.x, testPos.y, footPos.z), world)) { 
                position.y=newPosition.y;
                footPos=getFootPosition();
            } 
        } 
    }
    float dz=newPosition.z-position.z;
    if(dz!=0.0f) { 
        glm::vec3 testPos=footPos;testPos.z=footPos.z+dz;
        if(!checkCollision(glm::vec3(footPos.x, footPos.y, testPos.z), world)) 
            position.z=newPosition.z;
        else { 
            if(!isStepping) {
                glm::vec3 stepTestPos(footPos.x, footPos.y+STEP_HEIGHT, testPos.z);
                if(!checkCollision(stepTestPos, world)) { 
                    isStepping=true;
                    stepElapsed=0.0f;
                    stepStartY=position.y;
                    stepTargetDeltaY=STEP_HEIGHT;
                    stepPendZ=dz;
                }
            } else { 
                stepPendZ += dz;
            }
        }
    }
}

void Camera::updatePhysics(float deltaTime, World& world) {
    if(movementMode==FLY_MODE) {
        //飞行模式：无重力，允许垂直自由控制
        verticalVelocity=0.0f;
        return;
    }
    //重力模式：应用重力和浮力
    //判断玩家脚部是否淹没在水中
    glm::vec3 foot=getFootPosition();
    int fx=(int)floor(foot.x);
    int fy=(int)floor(foot.y);
    int fz=(int)floor(foot.z);
    bool submerged=false;
    if(fy>=0) {
        BlockType b=world.getBlock(fx, fy, fz);
        submerged=(b==WATER);
    }

    float g=gravityAcc;
    if(submerged) {
        //浮力降低有效重力
        g=gravityAcc*(1.0f-buoyancyFactor);
    }
    //积分垂直速度和位置
    verticalVelocity += g*deltaTime;
    float dy=verticalVelocity*deltaTime;

    //尝试垂直移动，遵循碰撞
    glm::vec3 footPos=getFootPosition();
    glm::vec3 testPos=footPos;testPos.y += dy;
    if(!checkCollision(glm::vec3(testPos.x, testPos.y, testPos.z), world)) {
        position.y += dy;
    } else {
        //碰到地面或天花板，垂直速度归零
        verticalVelocity=0.0f;
    }
}
