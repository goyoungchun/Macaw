local Speed = 90

function Tick(DeltaTime)
    local Transform = Actor:GetActorTransform()
    local Rotation = Transform:GetRotator()
    Rotation.Yaw = Rotation.Yaw + Speed * DeltaTime
    Transform:SetRotator(Rotation)
    Actor:SetActorTransform(Transform)
end
