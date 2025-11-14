#pragma once
#include "Common.h"

void loadBlockTextures();
void loadPanoramaTextures();//传统一次性加载全部全景贴图

//增量启动画面加载器
void initPanoramaTextureHandles();
bool loadPanoramaTextureIndex(int index);//同步加载请求将像素数据推入待上传队列）
void requestPanoramaLoad(int index);//异步请求

//标题纹理
void requestTitleTextureLoad();
void initTitleTextureHandle();

//副标题纹理
void requestSubtitleTextureLoad();
void initSubtitleTextureHandle();

//增量方块纹理加载器
void initBlockTextureHandles();
bool loadBlockTextureIndex(int index);
void requestBlockTextureLoad(int index);

//异步加载器：启动后台线程读取图片数据，主线程应调用 processPendingTextureUploads() 进行 GL 上传
void startTextureLoader();
void processPendingTextureUploads(int maxUploads=2);
void stopTextureLoader();

//查询是否有待上传的图片
bool hasPendingTextureUploads();

//球体纹理（足球）
void initSphereTextureHandle();
void requestSphereTextureLoad();
