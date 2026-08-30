#include "gltf_io.h"
#include <cgltf_write.h>
#include "memory/memory.h"
#include <stb_ds.h>

#define NODES_INITIAL_CAP 16

//node
int node_add(TwinStudio_GltfIo_Context* ctx, char* name);
void node_set_parent(TwinStudio_GltfIo_Context* ctx, int child, int parent);
void node_set_position(TwinStudio_GltfIo_Context* ctx, uint32_t index, float x, float y, float z);
void node_set_rotation(TwinStudio_GltfIo_Context* ctx, uint32_t index, float x, float y, float z, float w);

//project
void initialize(TwinStudio_GltfIo_Context* ctx);
void finalize(TwinStudio_GltfIo_Context* ctx);
void free_resources(TwinStudio_GltfIo_Context* ctx);


int test(TwinStudio_RenderBody* body) {
	TwinStudio_GltfIo_Context ctx;
	initialize(&ctx);
	
	int root_idx = node_add(&ctx, "root");
	int skeleton_idx = node_add(&ctx, "skeleton");
	int joint_map[256];
	memset(joint_map, 0, sizeof(joint_map));
	int joint_cnt = arrlen(body->joints);
	for (int i = 0; i < joint_cnt; ++i) {
		char joint_name[256];
		sprintf_s(joint_name, sizeof(joint_name), "joint_%d", i);
		joint_map[i] = node_add(&ctx, joint_name);
	}
	for (int i = 0; i < joint_cnt; ++i) {
		int nodeIndex = joint_map[i];
		if (body->joints[i].parentId == 255) {
			node_set_parent(&ctx, nodeIndex, skeleton_idx);
		}
		else {
			node_set_parent(&ctx, nodeIndex, joint_map[body->joints[i].parentId]);
		}
		node_set_position(&ctx, nodeIndex, body->joints[i].localTranslation.x, body->joints[i].localTranslation.y, body->joints[i].localTranslation.z);
		node_set_rotation(&ctx, nodeIndex, body->joints[i].localRotation.x, body->joints[i].localRotation.y, body->joints[i].localRotation.z, body->joints[i].localRotation.w);
	}

	finalize(&ctx);
	cgltf_result result = cgltf_write_file(&ctx.options, "D:\\test.gltf", &ctx.data);

	TwinStudio_ArenaFree(&ctx.arena);
	TwinStudio_ArrayManager_Terminate(&ctx.arrayMng);
}

int node_add(TwinStudio_GltfIo_Context* ctx, char* name) {
	int idx = arrlen(ctx->data.nodes);
	mng_arrput(&ctx->arrayMng, ctx->data.nodes, (cgltf_node) { 0 });
	ctx->data.nodes_count += 1;
	ctx->data.nodes[idx].name = TwinStudio_ArenaAlloc(&ctx->arena, strlen(name) + 1);
	strcpy(ctx->data.nodes[idx].name, name);

	TwinStudio_GltfIo_NodeProject node_project;
	memset(&node_project, 0, sizeof(TwinStudio_GltfIo_NodeProject));
	node_project.index = idx;
	node_project.mesh_index = -1;
	node_project.parent_index = -1;
	mng_arrput(&ctx->arrayMng, ctx->project.nodes, node_project);

	return idx;
}

void node_set_parent(TwinStudio_GltfIo_Context* ctx, int child, int parent) {
	ctx->project.nodes[child].parent_index = parent;
}

void node_set_position(TwinStudio_GltfIo_Context* ctx, uint32_t index, float x, float y, float z) {
	ctx->data.nodes[index].has_translation = 1;
	ctx->data.nodes[index].translation[0] = x;
	ctx->data.nodes[index].translation[1] = y;
	ctx->data.nodes[index].translation[2] = z;
}

void node_set_rotation(TwinStudio_GltfIo_Context* ctx, uint32_t index, float x, float y, float z, float w) {
	ctx->data.nodes[index].has_rotation = 1;
	ctx->data.nodes[index].rotation[0] = x;
	ctx->data.nodes[index].rotation[1] = y;
	ctx->data.nodes[index].rotation[2] = z;
	ctx->data.nodes[index].rotation[3] = w;
}

void initialize(TwinStudio_GltfIo_Context* ctx) {
	memset(ctx, 0, sizeof(TwinStudio_GltfIo_Context));
	ctx->arena = TwinStudio_CreateArena(1024 * 1024);

	ctx->options.type = cgltf_file_type_gltf;
	ctx->data.asset.version = "2.0";
	ctx->data.scene = TwinStudio_ArenaAlloc(&ctx->arena, sizeof(cgltf_scene));
	ctx->data.scene->name = "default";

	ctx->data.scenes = ctx->data.scene;
	ctx->data.scenes_count = 1;
}

void finalize(TwinStudio_GltfIo_Context* ctx) {
	int node_count = arrlen(ctx->project.nodes);
	for (int i = 0; i < node_count; ++i) {
		cgltf_node* node = &ctx->data.nodes[i];
		TwinStudio_GltfIo_NodeProject* node_project = &ctx->project.nodes[i];
		if (node_project->parent_index != -1) {
			cgltf_node* parent = &ctx->data.nodes[node_project->parent_index];
			node->parent = parent;
			mng_arrput(&ctx->arrayMng, parent->children, node);
			parent->children_count += 1;
		}
		else {
			mng_arrput(&ctx->arrayMng, ctx->data.scene->nodes, node);
			ctx->data.scene->nodes_count += 1;
		}
	}
	ctx->data.scene->nodes_count = arrlen(ctx->data.scene->nodes);
}

void TwinStudio_ArrayManager_Register(TwinStudio_ArrayManager* mng, void* arrayOld, void* arrayNew) {
	int cnt = arrlen(mng->arrays);
	if (arrayOld == NULL) {
		arrput(mng->arrays, arrayNew);
	}
	else {
		int pos = -1;
		for (int i = 0; i < cnt; ++i) {
			if (mng->arrays[i] == arrayOld) {
				pos = i;
				break;
			}
		}
		if (pos != -1) {
			mng->arrays[pos] = arrayNew;
		} else {
			arrput(mng->arrays, arrayNew);
		}
	}
}

void TwinStudio_ArrayManager_Terminate(TwinStudio_ArrayManager* mng) {
	int cnt = arrlen(mng->arrays);
	for (int i = 0; i < cnt; ++i) {
		arrfree(mng->arrays[i]);
	}
	arrfree(mng->arrays);
}