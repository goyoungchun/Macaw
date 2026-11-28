#include "Engine/FPrimitiveSceneProxy.h"

using namespace Engine;

FPrimitiveSceneProxy::~FPrimitiveSceneProxy() = default;

EVertexFactoryType FPrimitiveSceneProxy::GetVertexFactoryType() const {
    return EVertexFactoryType::Static;
}

