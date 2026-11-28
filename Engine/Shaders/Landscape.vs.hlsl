#include "LandscapeVertex.hlsli"
#include "MeshVertex.hlsli"

FPixelInput VertexMain(FMeshVertexInput Input) {
    return BuildPixelInput(Input);
}
