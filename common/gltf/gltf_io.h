#pragma once

#include <cgltf.h>
#include <render/mesh.h>

int test(TwinStudio_RenderBody* body);

typedef struct TwinStudio_GltfIo_NodeProject {
	int32_t index;
	int32_t parent_index;
	int32_t mesh_index;
} TwinStudio_GltfIo_NodeProject;

typedef struct TwinStudio_GltfIo_MeshProject {
	int32_t index;
} TwinStudio_GltfIo_MeshProject;

typedef struct TwinStudio_GltfIo_MaterialProject {
	int32_t index;
} TwinStudio_GltfIo_MaterialProject;

typedef struct TwinStudio_GltfIo_Project {
	TwinStudio_GltfIo_NodeProject* nodes;
} TwinStudio_GltfIo_Project;

typedef struct TwinStudio_ArrayManager {
	void** arrays;
} TwinStudio_ArrayManager;

typedef struct TwinStudio_GltfIo_Context {
	TwinStudio_ArrayManager arrayMng;
	TwinStudio_Arena arena;
	TwinStudio_GltfIo_Project project;
	cgltf_data data;
	cgltf_options options;
} TwinStudio_GltfIo_Context;

void TwinStudio_ArrayManager_Register(TwinStudio_ArrayManager* mng, void* arrayOld, void* arrayNew);
void TwinStudio_ArrayManager_Terminate(TwinStudio_ArrayManager* mng);

#define mng_arrput(mng, a, k) 	{\
	void* oldAddr = a;\
	arrput(a, k);\
	void* newAddr = a;\
	TwinStudio_ArrayManager_Register(mng, oldAddr, newAddr);\
}