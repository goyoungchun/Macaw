local State = {
    mAlternatePunch = false,
    mAlternateHit = false,
    mRagdollEnabledRegions = {},
    mRagdollTargets = {},
    mRagdollWeights = {},
}

local Settings = {
    MoveSpeed = 3.5,
    WalkSpeed = 1.5,
    SprintSpeed = 6.0,
    Acceleration = 24.0,
    JumpSpeed = 6.0,
    Gravity = 15.0,
    TurnSpeed = 10.0,
    StepHeight = 0.35,
    StepForwardDistance = 0.15,
    FloorProbeDistance = 0.05,
    WalkableFloorZ = 0.7,
    CameraDistance = 4.0,
    LookSensitivity = 0.003,
    RagdollBlendInSeconds = 0.12,
    RagdollBlendOutSeconds = 0.25,
}

local Clip = {
    Hit_Chest = 7,
    Hit_Head = 8,
    Interact = 12,
    Pistol_Reload = 22,
    Pistol_Shoot = 23,
    Punch_Cross = 24,
    Punch_Jab = 25,
    Roll = 27,
    Spell_Simple_Shoot = 35,
    Sword_Attack = 39,
}

local UnitY = Vector.new(0, 1, 0)
local UnitZ = Vector.new(0, 0, 1)
local Back = Vector.new(-1, 0, 0)

local LeftArmBodies = {"upperarm_l", "lowerarm_l", "hand_l"}
local RightArmBodies = {"upperarm_r", "lowerarm_r", "hand_r"}
local LeftLegBodies = {"thigh_l", "calf_l", "foot_l"}
local RightLegBodies = {"thigh_r", "calf_r", "foot_r"}
local NeckBodies = {"neck_02"}
local TorsoBodies = {"spine_02", "spine_04", "clavicle_l", "clavicle_r"}
local RagdollRegions = {
    {mKey = string.byte("1"), mBodyGroups = {LeftArmBodies}},
    {mKey = string.byte("2"), mBodyGroups = {RightArmBodies}},
    {mKey = string.byte("3"), mBodyGroups = {LeftLegBodies}},
    {mKey = string.byte("4"), mBodyGroups = {RightLegBodies}},
    {mKey = string.byte("5"), mBodyGroups = {NeckBodies}},
    {mKey = string.byte("6"), mBodyGroups = {TorsoBodies, NeckBodies, LeftArmBodies, RightArmBodies}},
}

local function Clamp(Value, Minimum, Maximum)
    return math.max(Minimum, math.min(Value, Maximum))
end

local function WrapAngle(Angle)
    return (Angle + math.pi) % (2 * math.pi) - math.pi
end

local function Axis(Positive, Negative)
    return (Input.IsDown(Positive) and 1 or 0) - (Input.IsDown(Negative) and 1 or 0)
end

local function UpdateAnimation()
    State.mMesh:SetAnimGraphFloat("Speed", State.mSpeed)
    State.mMesh:SetAnimGraphFloat("VerticalSpeed", State.mVerticalSpeed)
    State.mMesh:SetAnimGraphFloat("AimPitch", Clamp(State.mPitch / 0.58, -1, 1))
    State.mMesh:SetAnimGraphFloat("AimWeight", Clamp(1 - State.mSpeed / 5, 0.45, 1))
    State.mMesh:SetAnimGraphBool("IsInAir", not State.mGrounded)
    State.mMesh:SetAnimGraphBool("IsCrouching", State.mCrouch)
    State.mMesh:SetAnimGraphBool("IsSwimming", State.mSwim)
    State.mMesh:SetAnimGraphBool("IsAiming", State.mAim)
    State.mMesh:SetAnimGraphBool("WantsAction", State.mAction)
    State.mMesh:SetAnimGraphBool("WantsHit", State.mHit)
    State.mMesh:SetAnimGraphBool("IsDancing", State.mDance)
    State.mMesh:SetAnimGraphBool("IsDead", State.mDead)
end

local function UpdateRagdollTargets()
    local Changed = false
    for Index, Region in ipairs(RagdollRegions) do
        if Input.WasPressed(Region.mKey) then
            State.mRagdollEnabledRegions[Index] = not State.mRagdollEnabledRegions[Index]
            Changed = true
        end
    end

    if not Changed then
        return
    end

    State.mRagdollTargets = {}
    for Index, Region in ipairs(RagdollRegions) do
        if State.mRagdollEnabledRegions[Index] then
            for _, BodyGroup in ipairs(Region.mBodyGroups) do
                for _, Bone in ipairs(BodyGroup) do
                    State.mRagdollTargets[Bone] = true
                end
            end
        end
    end
end

local function UpdatePartialRagdoll(Step)
    UpdateRagdollTargets()

    for Bone in pairs(State.mRagdollTargets) do
        if State.mRagdollWeights[Bone] == nil and State.mMesh:SetBodySimulatePhysics(Bone, true) then
            State.mMesh:SetBodyPhysicsBlendWeight(Bone, 0.0)
            State.mRagdollWeights[Bone] = 0.0
        end
    end

    for Bone, Weight in pairs(State.mRagdollWeights) do
        local Enabled = State.mRagdollTargets[Bone]
        local NextWeight = Enabled and math.min(Weight + Step / Settings.RagdollBlendInSeconds, 1.0) or math.max(Weight - Step / Settings.RagdollBlendOutSeconds, 0.0)
        if NextWeight ~= Weight then
            State.mMesh:SetBodyPhysicsBlendWeight(Bone, NextWeight)
        end

        if not Enabled and NextWeight == 0.0 then
            State.mMesh:SetBodySimulatePhysics(Bone, false)
            State.mRagdollWeights[Bone] = nil
        else
            State.mRagdollWeights[Bone] = NextWeight
        end
    end
end

local function ResetPartialRagdoll()
    for Bone in pairs(State.mRagdollWeights) do
        State.mMesh:SetBodyPhysicsBlendWeight(Bone, 0.0)
        State.mMesh:SetBodySimulatePhysics(Bone, false)
    end

    State.mRagdollEnabledRegions = {}
    State.mRagdollTargets = {}
    State.mRagdollWeights = {}
end


local function ResetPlayer()
    ResetPartialRagdoll()
    Actor:SetActorTransform(State.mSpawn)
    State.mVelocity = Vector.new(0, 0, 0)
    State.mVerticalSpeed = 0
    State.mFacing = 0
    State.mYaw = 0
    State.mPitch = -0.22
    State.mCameraArm = 0
    State.mSpeed = 0
    State.mBusyAge = 0
    State.mGrounded = true
    State.mCrouch = false
    State.mSwim = false
    State.mAim = false
    State.mDance = false
    State.mDead = false
    State.mAction = false
    State.mHit = false
    State.mBusy = ""
    State.mActionName = ""

    UpdateAnimation()
end

local function StartAction(Name)
    State.mActionName = Name
    State.mMesh:SetAnimGraphFloat("ActionClip", Clip[Name])
    State.mMesh:RestartAnimGraphPlayer("Action Player")
    State.mAction = true
    State.mHit = false
    State.mDance = false
    State.mBusy = "Action"
    State.mBusyAge = 0
end

local function StartHit()
    State.mMesh:SetAnimGraphFloat("HitClip", Clip[State.mAlternateHit and "Hit_Head" or "Hit_Chest"])
    State.mMesh:RestartAnimGraphPlayer("Hit Player")
    State.mAlternateHit = not State.mAlternateHit
    State.mHit = true
    State.mAction = false
    State.mDance = false
    State.mBusy = "Hit"
    State.mBusyAge = 0
end

local function UpdateControls()
    if Input.WasPressed(Key.Delete) then
        State.mDead = true
        State.mDance = false
        State.mBusy = ""
        State.mAction = false
        State.mHit = false
    end

    if State.mDead then
        return
    end

    if Input.WasPressed(Key.C) and State.mGrounded then
        State.mCrouch = not State.mCrouch
        State.mSwim = false
    end

    if Input.WasPressed(Key.V) and State.mGrounded then
        State.mSwim = not State.mSwim
        State.mCrouch = false
    end

    State.mAim = not State.mSwim and not State.mCrouch and Input.IsDown(Key.MouseRight)

    if Input.WasPressed(Key.H) and State.mGrounded then
        StartHit()
    elseif State.mBusy == "" and State.mGrounded and not State.mSwim then
        if Input.WasPressed(Key.F) then
            State.mDance = not State.mDance
        elseif Input.WasPressed(Key.Q) then
            StartAction("Roll")
        elseif Input.WasPressed(Key.R) then
            StartAction("Pistol_Reload")
        elseif Input.WasPressed(Key.E) then
            StartAction("Interact")
        elseif Input.WasPressed(Key.X) then
            StartAction("Sword_Attack")
        elseif Input.WasPressed(Key.K) then
            StartAction("Spell_Simple_Shoot")
        elseif Input.WasPressed(Key.MouseLeft) then
            StartAction(State.mAim and "Pistol_Shoot" or (State.mAlternatePunch and "Punch_Cross" or "Punch_Jab"))
            State.mAlternatePunch = not State.mAlternatePunch
        end
    end

    if Input.WasPressed(Key.Space) and State.mGrounded and State.mBusy == "" and not State.mSwim and not State.mDance then
        State.mGrounded = false
        State.mVerticalSpeed = Settings.JumpSpeed
    end
end

local function SweepMove(Delta)
    return select(2, Actor:AddActorWorldOffset(Delta, true))
end

local function IsWalkable(Hit)
    return Hit.BlockingHit and Hit.Normal.z >= Settings.WalkableFloorZ
end

local function SlideMove(Delta, Hit)
    for Attempt = 1, 3 do
        if not Hit.BlockingHit then
            return
        end

        local Remaining = Delta * (1 - Hit.Time)
        local Normal = Hit.Normal
        if not IsWalkable(Hit) then
            Normal = Vector.new(Normal.x, Normal.y, 0)
            if Normal:LengthSquared() < 0.000001 then
                return
            end
            Normal = Normal:Normalized()
        end

        local IntoSurface = Remaining.x * Normal.x + Remaining.y * Normal.y + Remaining.z * Normal.z
        Delta = Remaining - Normal * IntoSurface
        if Delta:LengthSquared() < 0.00000001 then
            return
        end

        Hit = SweepMove(Delta)
    end
end

local function TryStepUp(Delta)
    local Distance = Delta:Length()
    if Distance < 0.0001 then
        return false
    end

    local Start = Actor:GetActorLocation()
    local Ceiling = SweepMove(UnitZ * Settings.StepHeight)
    if Ceiling.BlockingHit then
        Actor:SetActorLocation(Start)
        return false
    end

    local Raised = Actor:GetActorLocation()
    local Forward = Delta * (math.max(Distance, Settings.StepForwardDistance) / Distance)
    SweepMove(Forward)
    local Progress = Actor:GetActorLocation() - Raised
    local Floor = SweepMove(UnitZ * -(Settings.StepHeight + Settings.FloorProbeDistance))

    if Progress:LengthSquared() > 0.000001 and IsWalkable(Floor) and Actor:GetActorLocation().z > Start.z + Settings.FloorProbeDistance then
        return true
    end

    Actor:SetActorLocation(Start)
    return false
end

local function MoveHorizontal(Delta)
    if Delta:LengthSquared() < 0.00000001 then
        return
    end

    local Hit = SweepMove(Delta)
    if State.mGrounded and Hit.BlockingHit and not IsWalkable(Hit) and TryStepUp(Delta * (1 - Hit.Time)) then
        return
    end

    SlideMove(Delta, Hit)
end

local function MoveVertical(Distance)
    local Hit = SweepMove(UnitZ * Distance)
    if Distance > 0 then
        if Hit.BlockingHit then
            State.mVerticalSpeed = 0
        end
        State.mGrounded = false
        return
    end

    if not Hit.BlockingHit and State.mGrounded then
        local Start = Actor:GetActorLocation()
        local Floor = SweepMove(UnitZ * -Settings.StepHeight)
        if IsWalkable(Floor) then
            Hit = Floor
        else
            Actor:SetActorLocation(Start)
        end
    end

    State.mGrounded = IsWalkable(Hit)
    if State.mGrounded then
        State.mVerticalSpeed = 0
    end
end

local function MovePlayer(Step)
    local Forward = Vector.new(math.cos(State.mYaw), math.sin(State.mYaw), 0)
    local Right = Vector.new(-Forward.y, Forward.x, 0)
    local Direction = Forward * Axis(Key.W, Key.S) + Right * Axis(Key.D, Key.A)
    if Direction:LengthSquared() > 1 then
        Direction = Direction:Normalized()
    end

    local Sprint = Input.IsDown(Key.Shift) or Input.IsDown(Key.LeftShift) or Input.IsDown(Key.RightShift)
    local TargetSpeed = State.mSwim and 2.2 or ((State.mCrouch or State.mAim or Input.IsDown(Key.Z)) and Settings.WalkSpeed or (Sprint and Settings.SprintSpeed or Settings.MoveSpeed))
    local Rolling = State.mBusy == "Action" and State.mActionName == "Roll"
    local Blocked = State.mDead or State.mDance or (State.mBusy ~= "" and not Rolling)
    local Desired = Direction * TargetSpeed
    if Blocked then
        Desired = Vector.new(0, 0, 0)
    elseif Rolling then
        Desired = Vector.new(math.cos(State.mFacing), math.sin(State.mFacing), 0) * 4.4
    end

    local Difference = Desired - State.mVelocity
    local Change = Settings.Acceleration * (State.mGrounded and 1 or 0.35) * Step
    State.mVelocity = State.mVelocity + (Difference:LengthSquared() > Change * Change and Difference:Normalized() * Change or Difference)

    if State.mAim or (not Blocked and not Rolling and Direction:LengthSquared() > 0.0001) then
        local Target = State.mAim and State.mYaw or math.atan2(Direction.y, Direction.x)
        local Turn = WrapAngle(Target - State.mFacing)
        local MaxTurn = Settings.TurnSpeed * Step
        State.mFacing = WrapAngle(State.mFacing + Clamp(Turn, -MaxTurn, MaxTurn))
    end

    local Previous = Actor:GetActorLocation()
    MoveHorizontal(State.mVelocity * Step)
    State.mVerticalSpeed = State.mVerticalSpeed - Settings.Gravity * Step
    MoveVertical(State.mVerticalSpeed * Step)

    local Player = Actor:GetActorTransform()
    local Position = Player.Location
    if Position.z < -10 then
        ResetPlayer()
        return
    end

    local Horizontal = Vector.new(Position.x - Previous.x, Position.y - Previous.y, 0)
    State.mSpeed = Step > 0 and math.min(Horizontal:Length() / Step, State.mVelocity:Length()) or 0
    Player.Rotation = Quat.new(UnitZ, State.mFacing) * State.mSpawn.Rotation
    Actor:SetActorTransform(Player)
end

local function UpdateCamera(Step)
    local Rotation = (Quat.new(UnitZ, State.mYaw) * Quat.new(UnitY, -State.mPitch)):Normalized()
    local Target = Actor:GetActorLocation() + UnitZ * (State.mCrouch and 0.22 or 0.5)
    local Direction = Rotation:RotateVector(Back)
    local Obstacle = World.Raycast(Target, Direction, Actor)
    local Distance = Settings.CameraDistance * (State.mAim and 0.75 or 1)
    local Limit = Obstacle.IsHit and Clamp(Obstacle.Distance - 0.20, 0.20, Distance) or Distance
    State.mCameraArm = State.mCameraArm <= 0 and Limit or math.min(Limit, State.mCameraArm + Step * 8)

    local Camera = State.mCamera:GetActorTransform()
    Camera.Location = Target + Direction * State.mCameraArm
    Camera.Rotation = Rotation
    State.mCamera:SetActorTransform(Camera)
end

function BeginPlay()
    assert(Actor:GetComponentByClass("UCapsuleComponent"), "The third person character needs a movement capsule.")
    State.mMesh = assert(Actor:GetComponentByClass("USkeletalMeshComponent"), "The third person character needs a skeletal mesh.")
    State.mCamera = assert(World.FindActor("Follow Camera"), "The follow camera is missing.")
    assert(World.SetActiveCamera(State.mCamera), "The follow camera needs a camera component.")
    State.mSpawn = Actor:GetActorTransform()

    ResetPlayer()
    UpdateCamera(0)
end

function Tick(DeltaTime)
    local Step = Clamp(DeltaTime, 0, 0.05)
    local MouseX, MouseY = Input.GetMouseDelta()
    State.mYaw = WrapAngle(State.mYaw + MouseX * Settings.LookSensitivity)
    State.mPitch = Clamp(State.mPitch - MouseY * Settings.LookSensitivity, -1.15, 0.9)
    State.mBusyAge = State.mBusyAge + Step

    if Input.WasPressed(Key.Backspace) then
        ResetPlayer()
    end

    UpdateControls()
    UpdatePartialRagdoll(Step)

    if State.mBusyAge > 0.12 and State.mBusy == "Action" and State.mMesh:IsAnimGraphPlayerFinished("Action Player") then
        State.mBusy = ""
        State.mAction = false
    end

    if State.mBusyAge > 0.12 and State.mBusy == "Hit" and State.mMesh:IsAnimGraphPlayerFinished("Hit Player") then
        State.mBusy = ""
        State.mHit = false
    end

    MovePlayer(Step)
    UpdateAnimation()
    UpdateCamera(Step)
end

function EndPlay()
    ResetPartialRagdoll()
end
