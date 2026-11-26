local State = {
    mAlternatePunch = false,
    mAlternateHit = false,
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
    WalkableFloorZ = 0.7,
    CameraDistance = 4.0,
    LookSensitivity = 0.003,
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

local function Clamp(Value, Minimum, Maximum)
    return math.max(Minimum, math.min(Value, Maximum))
end

local function WrapAngle(Angle)
    return (Angle + math.pi) % (2 * math.pi) - math.pi
end

local function Axis(Positive, Negative)
    return (Input.IsDown(Positive) and 1 or 0) - (Input.IsDown(Negative) and 1 or 0)
end

local function Dot(A, B)
    return A.x * B.x + A.y * B.y + A.z * B.z
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

local function ResetPlayer()
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

-- Delta를 쓸어 옮기다 Hit에서 막혔을 때, 남은 이동을 막은 면을 따라 미끄러뜨립니다.
local function SlideMove(Delta, Hit)
    for _ = 1, 3 do
        if not Hit.BlockingHit then
            return
        end

        local Remaining = Delta * (1 - Hit.Time)
        local Normal = Hit.Normal

        -- 걸을 수 없는 면(벽)에서는 위로 타고 오르지 않도록 수평 성분만으로 미끄러집니다.
        if Normal.z < Settings.WalkableFloorZ then
            Normal = Vector.new(Normal.x, Normal.y, 0)
            if Normal:LengthSquared() < 0.000001 then
                return
            end
            Normal = Normal:Normalized()
        end

        Delta = Remaining - Normal * Dot(Remaining, Normal)
        if Delta:LengthSquared() < 0.00000001 then
            return
        end

        _, Hit = Actor:AddActorWorldOffset(Delta, true)
    end
end

-- 계단처럼 낮은 턱에 막히면 StepHeight만큼 올라가 이동한 뒤 내려와, 딛을 수 있는 바닥이면 받아들입니다.
local function TryStepUp(Delta)
    local Start = Actor:GetActorLocation()
    Actor:AddActorWorldOffset(UnitZ * Settings.StepHeight, true)
    local Raised = Actor:GetActorLocation().z - Start.z
    local _, Forward = Actor:AddActorWorldOffset(Delta, true)
    local _, Floor = Actor:AddActorWorldOffset(UnitZ * -(Raised + 0.05), true)

    local Advanced = Actor:GetActorLocation() - Start
    local Progress = Vector.new(Advanced.x, Advanced.y, 0):LengthSquared()
    if not Floor.BlockingHit or Floor.Normal.z < Settings.WalkableFloorZ or (Forward.BlockingHit and Progress < 0.000001) then
        Actor:SetActorLocation(Start)
        return false
    end

    return true
end

-- 캡슐(루트 컴포넌트)을 PhysX로 쓸어 수평으로 옮깁니다. 낮은 턱은 올라서고, 나머지는 막은 면을 따라 미끄러집니다.
local function MoveHorizontal(Delta)
    if Delta:LengthSquared() < 0.0000000001 then
        return
    end

    local _, Hit = Actor:AddActorWorldOffset(Delta, true)
    if not Hit.BlockingHit then
        return
    end

    if State.mGrounded and Hit.Normal.z < Settings.WalkableFloorZ and TryStepUp(Delta * (1 - Hit.Time)) then
        return
    end

    SlideMove(Delta, Hit)
end

-- 아래로 쓸어 바닥을 찾습니다. 서 있던 중이면 StepHeight까지 내려가 계단과 경사로를 따라 붙습니다.
local function MoveVertical(VerticalDistance)
    if VerticalDistance > 0 then
        local _, Ceiling = Actor:AddActorWorldOffset(UnitZ * VerticalDistance, true)
        if Ceiling.BlockingHit then
            State.mVerticalSpeed = 0
        end
        State.mGrounded = false
        return
    end

    local Start = Actor:GetActorLocation()
    local Probe = State.mGrounded and math.min(VerticalDistance, -Settings.StepHeight) or VerticalDistance
    local _, Floor = Actor:AddActorWorldOffset(UnitZ * Probe, true)

    if Floor.BlockingHit and Floor.Normal.z >= Settings.WalkableFloorZ then
        State.mGrounded = true
        State.mVerticalSpeed = 0
        return
    end

    -- 바닥을 붙잡으려고 더 내려간 만큼은 되돌리고 낙하를 시작합니다.
    if State.mGrounded and not Floor.BlockingHit then
        Actor:SetActorLocation(Start + UnitZ * VerticalDistance)
    end
    State.mGrounded = false
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
    MoveHorizontal(Vector.new(State.mVelocity.x, State.mVelocity.y, 0) * Step)

    State.mVerticalSpeed = State.mVerticalSpeed - Settings.Gravity * Step
    MoveVertical(State.mVerticalSpeed * Step)

    local Position = Actor:GetActorLocation()
    local Horizontal = Vector.new(Position.x - Previous.x, Position.y - Previous.y, 0)
    State.mSpeed = Step > 0 and Horizontal:Length() / Step or 0

    if Position.z < -10 then
        ResetPlayer()
        return
    end

    -- 회전은 막힐 일이 없으므로 쓸지 않고 루트 캡슐만 돌립니다. 메시는 캡슐을 따라 함께 돕니다.
    local Player = Actor:GetActorTransform()
    Player.Rotation = Quat.new(UnitZ, State.mFacing) * State.mSpawn.Rotation
    Actor:SetActorTransform(Player)
end

local function UpdateCamera(Step)
    local Rotation = (Quat.new(UnitZ, State.mYaw) * Quat.new(UnitY, -State.mPitch)):Normalized()
    -- 루트가 캡슐이므로 액터 위치는 캡슐 중심(발에서 0.9 m 위)입니다.
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
