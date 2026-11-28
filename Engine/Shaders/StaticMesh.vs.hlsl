#ifndef MacawLandscape
#define MacawInstanced 1
#endif
#include "MeshVertex.hlsli"
#include "LandscapeVertex.hlsli"

FPixelInput VertexMain(FMeshVertexInput Input) {
#ifdef MacawLandscape
	DisplaceLandscapeVertex(Input.mPosition, Input.mNormal, Input.mTangent, Input.mUV);
#endif
	return BuildPixelInput(Input);
}
