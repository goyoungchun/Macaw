local State = {}
local X = Vector.new(1, 0, 0)
local Y = Vector.new(0, 1, 0)
local Z = Vector.new(0, 0, 1)
local Zero = Vector.new(0, 0, 0)
local Down = Vector.new(0, 0, -1)
local Back = Vector.new(-1, 0, 0)

local GateDistances = {10, 22, 34}
local FocusDistances = {0.98, 3.26, 9.48}
local FocusNames = {"NEAR", "MID", "FAR"}
local ArmBones = {"upperarm_r", "lowerarm_r"}
local EchoMesh = "/Game/ApertureRelay/Echo/EchoAperture.masset"
local EchoSkeleton = "/Game/Characters/Echo/Echo_Combined_Embedded/Echo_Combined_Embedded_Skeleton.masset"
local RigPosition = Vector.new(4.4, 3.35, 0)
local DockPosition = Vector.new(40, -2, 0)
local BenchPosition = Vector.new(43.05, 2.3, 0)
local BenchCameraPosition = Vector.new(42.39, 3.5, 1.73)

local function Clamp(Value, Minimum, Maximum)
    return math.max(Minimum, math.min(Value, Maximum))
end

local function Wrap(Angle)
    return (Angle + math.pi) % (2 * math.pi) - math.pi
end

local function Axis(Positive, Negative)
    return (Input.IsDown(Positive) and 1 or 0) - (Input.IsDown(Negative) and 1 or 0)
end

local function Near(A, B, Radius)
    local Dx, Dy = A.x - B.x, A.y - B.y
    return Dx * Dx + Dy * Dy <= Radius * Radius
end

local function Find(Name)
    local Target = World.FindActor(Name)
    assert(Target, Name .. " is missing from Aperture Relay")
    return Target
end

local function Component(Owner, Name)
    local Target = Owner:GetComponentByName(Name)
    assert(Target, Name .. " is missing on " .. Owner:GetName())
    return Target
end

local function SetReadout(Objective, Prompt, Readout)
    if State.mObjective ~= Objective then
        State.mLabels.Objective:SetText(Objective)
        State.mObjective = Objective
    end

    if State.mPrompt ~= Prompt then
        State.mLabels.Prompt:SetText(Prompt)
        State.mPrompt = Prompt
    end

    if State.mReadout ~= Readout then
        State.mLabels.Readout:SetText(Readout)
        State.mReadout = Readout
    end
end

local function SetBeam(Visible)
    State.mBeam:SetActorLocation(Visible and State.mBeamSpawn.Location or State.mBeamSpawn.Location - Z * 150)
end

local function SetOptics(Enabled)
    State.mCameraSettings.Enabled = Enabled and State.mBlur
    State.mCameraSettings.FStop = State.mFastStop and 1.2 or 11
    State.mCameraSettings.MaxBlurRadius = 22
    State.mCameraSettings.ApertureShape = DepthOfFieldApertureShape.Hexagon
    State.mCameraSettings.FocalDistance = State.mFocusDistance
    State.mLens:SetDepthOfFieldSettings(State.mCameraSettings)
    State.mLens:SetSensorHeight(Enabled and 43.4 or 24)
    State.mLens:SetFocalLength(Enabled and 40 or 22.1)
end

local function ResetWitness()
    State.mWitnessMesh:SetAllBodiesBelowPhysicsBlendWeight("pelvis", 0, true)
    State.mWitnessMesh:SetAllBodiesBelowSimulatePhysics("pelvis", false, true)
    State.mWitness:SetActorTransform(State.mWitnessSpawn)
    State.mWitnessFlying = false
    State.mWitnessAge = 0

    for _, Entry in ipairs(State.mCargo) do
        Entry.mActor:SetActorTransform(Entry.mSpawn)
        Entry.mBody:SetPhysicsVelocity(Zero, Zero)
    end
end

local function ResetEcho(Pose)
    State.mVelocity = Zero
    State.mVertical = 0
    State.mGrounded = true
    State.mSpeed = 0
    State.mCameraArm = 0
    State.mArmActive = false
    State.mArmBlend = 0
    State.mHitAge = 0

    for _, Bone in ipairs(ArmBones) do
        State.mMesh:SetBodyPhysicsBlendWeight(Bone, 0)
        State.mMesh:SetBodySimulatePhysics(Bone, false)
    end

    Actor:SetActorTransform(Pose)
end

local function ResetMission()
    State.mPhase = "Rig"
    State.mDriving = false
    State.mGate = 0
    State.mFocus = 1
    State.mSeen = {}
    State.mFocusDistance = FocusDistances[1]
    State.mFastStop = true
    State.mBlur = true
    State.mYaw = 0.14
    State.mFacing = 0
    State.mPitch = -0.15
    State.mTransmissionAge = 0
    for _, Pulse in ipairs(State.mPulses) do
        Pulse:SetActorLocation(State.mPulseStandby)
    end
    State.mDriveYaw = 0
    State.mDockSign:SetText("03   /   RELAY DOCK")
    State.mBenchSign:SetText("04   /   OPTICAL BENCH")
    State.mDrive:ResetDrive()
    State.mRover:SetActorTransform(State.mRoverSpawn)
    State.mChassis:SetPhysicsVelocity(Zero, Zero)
    State.mDrive:SetDriveInput(0, 1, 0)
    State.mBeam:SetActorTransform(State.mBeamSpawn)
    SetBeam(false)
    SetOptics(false)
    ResetEcho(State.mSpawn)
    ResetWitness()
end

local function FireRig()
    if State.mWitnessFlying then
        ResetWitness()
        return
    end

    if not State.mWitnessMesh:SetAllBodiesBelowSimulatePhysics("pelvis", true, true) then
        return
    end

    State.mWitnessMesh:SetAllBodiesBelowPhysicsBlendWeight("pelvis", 1, true)
    State.mWitnessMesh:AddImpulseToBody("pelvis", Vector.new(-3.5, 0.35, 3.05), true)
    State.mWitnessFlying = true
    State.mWitnessAge = 0
    State.mPhase = "Rover"

    for Index, Entry in ipairs(State.mCargo) do
        local Spread = (Index - 3.5) * 0.45
        Entry.mBody:SetPhysicsVelocity(Vector.new(-2.8, Spread + 0.25, 1.75 + Index * 0.14), Vector.new(2.1, Spread, 1.75))
    end
end

local function UpdateArm(Step)
    local Toggle = Input.WasPressed(Key.Q)
    if Toggle then
        State.mArmActive = not State.mArmActive

        if State.mArmActive then
            for _, Bone in ipairs(ArmBones) do
                State.mMesh:SetBodySimulatePhysics(Bone, true)
                State.mMesh:SetBodyPhysicsBlendWeight(Bone, State.mArmBlend)
            end
            local Impulse = Vector.new(-0.4, -1.45, 2.65)
            if not State.mMesh:AddImpulseToBody("lowerarm_r", Impulse, true) then
                State.mMesh:AddImpulseToBody("upperarm_r", Impulse, true)
            end
            State.mMesh:SetAnimGraphFloat("HitClip", 7)
            State.mMesh:RestartAnimGraphPlayer("Hit Player")
            State.mHitAge = 0.45
        end
    end

    local Goal = State.mArmActive and 1 or 0
    local Previous = State.mArmBlend
    State.mArmBlend = Goal == 1 and math.min(1, Previous + Step * 5) or math.max(0, Previous - Step * 3.5)

    if Previous ~= State.mArmBlend then
        for _, Bone in ipairs(ArmBones) do
            State.mMesh:SetBodyPhysicsBlendWeight(Bone, State.mArmBlend)
            if State.mArmBlend == 0 then
                State.mMesh:SetBodySimulatePhysics(Bone, false)
            end
        end
    end
end

local function UpdateAnimation(Step)
    State.mHitAge = math.max(0, State.mHitAge - Step)
    State.mMesh:SetAnimGraphFloat("Speed", State.mSpeed)
    State.mMesh:SetAnimGraphFloat("VerticalSpeed", State.mVertical)
    State.mMesh:SetAnimGraphBool("IsInAir", not State.mGrounded)
    State.mMesh:SetAnimGraphBool("WantsHit", State.mHitAge > 0)
end

local function MoveAlong(Position, Direction, Distance)
    if math.abs(Distance) < 0.00001 then
        return Position
    end

    local Trace = Direction * (Distance > 0 and 1 or -1)
    local Allowed = math.abs(Distance)
    for _, Height in ipairs({0.44, 1.48}) do
        local Hit = World.Raycast(Position + Z * Height, Trace, Actor)
        if Hit.IsHit then
            Allowed = math.min(Allowed, math.max(0, Hit.Distance - 0.29))
        end
    end

    return Position + Trace * Allowed
end

local function Walk(Step)
    local Forward = Vector.new(math.cos(State.mYaw), math.sin(State.mYaw), 0)
    local Right = Vector.new(-Forward.y, Forward.x, 0)
    local Direction = Forward * Axis(Key.W, Key.S) + Right * Axis(Key.D, Key.A)
    local Moving = Direction:LengthSquared() > 0
    local Desired = Moving and Direction:Normalized() * (Input.IsDown(Key.Shift) and 6.15 or 3.45) or Zero
    local Difference = Desired - State.mVelocity
    local MoveLimit = (State.mGrounded and 22 or 8) * Step

    State.mVelocity = State.mVelocity + (Difference:LengthSquared() > MoveLimit * MoveLimit and Difference:Normalized() * MoveLimit or Difference)
    if Moving then
        local Turn = Wrap(math.atan2(Direction.y, Direction.x) - State.mFacing)
        State.mFacing = Wrap(State.mFacing + Clamp(Turn, -10 * Step, 10 * Step))
    end

    if State.mGrounded and Input.WasPressed(Key.Space) then
        State.mGrounded = false
        State.mVertical = 5.85
    end

    local Pose = Actor:GetActorTransform()
    local Previous = Pose.Location
    local Position = MoveAlong(Previous, X, State.mVelocity.x * Step)
    Position = MoveAlong(Position, Y, State.mVelocity.y * Step)
    State.mSpeed = Step > 0 and (Position - Previous):Length() / Step or 0
    State.mVertical = State.mVertical - 15 * Step
    local Vertical = State.mVertical * Step
    if Vertical > 0 then
        local Hit = World.Raycast(Position + Z * 1.73, Z, Actor)
        if Hit.IsHit and Hit.Distance < Vertical + 0.04 then
            Vertical = math.max(0, Hit.Distance - 0.04)
            State.mVertical = 0
        end
    end

    Position = Position + Z * Vertical
    local Ground = World.Raycast(Vector.new(Position.x, Position.y, math.max(Previous.z, Position.z) + 0.32), Down, Actor)
    local Relief = State.mGrounded and 0.32 or 0
    local Height = Ground.IsHit and Ground.Position.z + 0.025 or -100
    State.mGrounded = State.mVertical <= 0 and Height <= Previous.z + Relief + 0.02 and Position.z <= Height + Relief
    if State.mGrounded then
        Position.z = Height
        State.mVertical = 0
    end

    if Position.z < -6 or Position.x < -23 or Position.x > 69 or math.abs(Position.y) > 25 then
        ResetEcho((State.mPhase == "Bench" or State.mPhase == "Online") and State.mBenchSpawn or State.mSpawn)
        return
    end

    Pose.Location = Position
    Pose.Rotation = Quat.new(Z, State.mFacing) * State.mSpawn.Rotation
    Actor:SetActorTransform(Pose)
end

local function SetCamera(Position, Rotation)
    local Pose = State.mCamera:GetActorTransform()
    Pose.Location = Position
    Pose.Rotation = Rotation
    State.mCamera:SetActorTransform(Pose)
end

local function LookRotation(Yaw, Pitch)
    return (Quat.new(Z, Yaw) * Quat.new(Y, -Pitch)):Normalized()
end

local function WalkCamera(Step)
    local Rotation = LookRotation(State.mYaw, State.mPitch)
    local ShoulderHeight = State.mPhase == "Online" and 1.86 or 1.43
    local Target = Actor.Location + Z * ShoulderHeight + Rotation:RotateVector(Y) * -0.45
    local Direction = Rotation:RotateVector(Back)
    local Hit = World.Raycast(Target, Direction, Actor)
    local CameraDistance = State.mPhase == "Online" and 6.15 or 4.3
    local Limit = Hit.IsHit and Clamp(Hit.Distance - 0.18, 0.25, CameraDistance) or CameraDistance
    State.mCameraArm = State.mCameraArm <= 0 and Limit or math.min(Limit, State.mCameraArm + Step * 9)
    SetCamera(Target + Direction * State.mCameraArm, Rotation)
end

local function EnterRover()
    if not State.mDrive:SetDriveInput(0, 1, 0) then
        return
    end

    local Forward = State.mRover:GetActorTransform().Rotation:RotateVector(X)
    State.mDriveYaw = math.atan2(Forward.y, Forward.x)
    State.mDriving = true
    Actor:SetActorLocation(State.mSpawn.Location - Z * 120)
end

local function PlaceEchoAt(Position)
    local Pose = Actor:GetActorTransform()
    local Ground = World.Raycast(Position + Z * 1.4, Down, Actor)
    Pose.Location = Vector.new(Position.x, Position.y, Ground.IsHit and Ground.Position.z + 0.025 or 0.025)
    ResetEcho(Pose)
    State.mFacing = State.mDriveYaw
    State.mYaw = State.mDriveYaw
    State.mPitch = -0.19
end

local function ParkRover(AtDock)
    local RoverPose = State.mRover:GetActorTransform()
    local Exit = RoverPose.Location + LookRotation(State.mDriveYaw, 0):RotateVector(Y) * 1.87

    State.mDriving = false
    State.mDrive:SetDriveInput(0, 1, 0)
    if AtDock then
        State.mPhase = "Bench"
        Exit = Vector.new(40.5, 0.52, 0.08)
        State.mDriveYaw = 0.48
    end

    PlaceEchoAt(Exit)
end

local function DriveRover(Step)
    local RoverPose = State.mRover:GetActorTransform()
    local Position = RoverPose.Location
    local Forward = RoverPose.Rotation:RotateVector(X)
    local Up = RoverPose.Rotation:RotateVector(Z)
    local Turn = Wrap(math.atan2(Forward.y, Forward.x) - State.mDriveYaw)
    State.mDriveYaw = Wrap(State.mDriveYaw + Turn * Clamp(Step * 6, 0, 1))

    if Position.z < -3 or Position.x < -24 or Position.x > 68 or math.abs(Position.y) > 24 or Up.z < 0.12 or Input.WasPressed(Key.R) then
        local Checkpoint = State.mRover:GetActorTransform()
        Checkpoint.Rotation = State.mRoverSpawn.Rotation
        Checkpoint.Location = Vector.new(State.mGate == 0 and 0 or GateDistances[State.mGate] + 3.2, -2, 0.83)
        State.mRover:SetActorTransform(Checkpoint)
        State.mChassis:SetPhysicsVelocity(Zero, Zero)
        State.mDrive:ResetDrive()
        State.mDriveYaw = 0
        State.mDrive:SetDriveInput(0, 1, 0)
        return
    end

    local Gate = GateDistances[State.mGate + 1]
    if Gate and math.abs(Position.x - Gate) < 1.65 and math.abs(Position.y + 2) < 4.0 then
        State.mGate = State.mGate + 1
    end

    local AtDock = State.mGate == #GateDistances and math.abs(Position.x - DockPosition.x) < 3.2 and math.abs(Position.y - DockPosition.y) < 2.35
    if Input.WasPressed(Key.E) then
        ParkRover(AtDock)
        return
    end

    local _, Speed = State.mDrive:GetDriveState()
    local Brake = Input.IsDown(Key.Space) and 1 or 0
    local Throttle = Axis(Key.W, Key.S)
    local Steering = Axis(Key.D, Key.A)
    local ApproachingDock = State.mGate >= 2 and Position.x > 23 and Position.x < 43 and math.abs(Position.y - DockPosition.y) < 4.2 and Forward.x > 0.55
    if ApproachingDock and Throttle >= 0 then
        local Distance = math.max(0, DockPosition.x - Position.x - 0.6)
        local SpeedLimit = math.max(2.2, math.sqrt(2.8 * Distance))
        Brake = math.max(Brake, Clamp((Speed - SpeedLimit) * 0.42, 0, 1))
        if AtDock and Position.x >= DockPosition.x - 0.65 then
            Throttle = 0
            Brake = 1
        end
    end

    State.mDrive:SetDriveInput(Throttle, Brake, Steering)
    local Rotation = LookRotation(State.mDriveYaw, -0.16)
    SetCamera(Position + Z * 1.0 + Rotation:RotateVector(Back) * 5.6, Rotation)

    local Objective = AtDock and "03   DOCK WINDOW OPEN" or "02   CROSS THE CALDERA"
    local Prompt = AtDock and "E  DOCK AND EXIT     S  REVERSE" or ApproachingDock and "DOCK APPROACH     WASD  DRIVE     R  RECOVER" or "WASD  DRIVE     SPACE  BRAKE     R  RECOVER"
    SetReadout(Objective, Prompt, string.format("GATES  %d / 3     %02d KM/H", State.mGate, math.floor(math.abs(Speed) * 3.6 + 0.5)))
end

local function BeginOptics()
    State.mPhase = "Optics"
    State.mFocus = 1
    State.mSeen[1] = true
    State.mFocusDistance = FocusDistances[1]
    SetOptics(true)
end

local function UpdateOptics(Step)
    for Index = 1, #FocusDistances do
        if Input.WasPressed(string.byte(tostring(Index))) then
            State.mFocus = Index
            State.mSeen[Index] = true
        end
    end

    local ApertureChanged = Input.WasPressed(Key.C)
    local BlurChanged = Input.WasPressed(Key.V)
    if ApertureChanged then
        State.mFastStop = not State.mFastStop
    end
    if BlurChanged then
        State.mBlur = not State.mBlur
    end
    if ApertureChanged or BlurChanged then
        SetOptics(true)
    end

    local Target = FocusDistances[State.mFocus]
    State.mFocusDistance = State.mFocusDistance + (Target - State.mFocusDistance) * Clamp(Step * 5.5, 0, 1)
    State.mLens:SetDepthOfFieldFocalDistance(State.mFocusDistance)
    SetCamera(BenchCameraPosition, LookRotation(0.035, 0.015))

    local Aligned = State.mSeen[1] and State.mSeen[2] and State.mSeen[3]
    if Aligned and Input.WasPressed(Key.F) then
        State.mPhase = "Online"
        State.mYaw = 0.05
        State.mPitch = 0.24
        State.mCameraArm = 0
        SetOptics(false)
        SetBeam(true)
        State.mDockSign:SetText("")
        State.mBenchSign:SetText("")
        return
    end

    local Prompt = Aligned and "F  SEND SIGNAL     C  F-STOP     V  DOF" or "1 / 2 / 3  FOCUS     C  F-STOP     V  DOF"
    local Readout = string.format("%s     %.2f M     F/%.1f     DOF %s", FocusNames[State.mFocus], State.mFocusDistance, State.mFastStop and 1.2 or 11, State.mBlur and "ON" or "OFF")
    SetReadout("04   ALIGN THE THREE FOCAL PLANES", Prompt, Readout)
end

local function UpdateTransmission(Step)
    State.mTransmissionAge = State.mTransmissionAge + Step

    local Beam = State.mBeam:GetActorTransform()
    Beam.Rotation = Quat.new(X, State.mTransmissionAge * 0.31) * State.mBeamSpawn.Rotation
    State.mBeam:SetActorTransform(Beam)

    for Index, Pulse in ipairs(State.mPulses) do
        local Delay = (Index - 1) * 0.57
        local Age = State.mTransmissionAge - Delay
        local Phase = (Age % 2.4) / 2.4
        if Age >= 0 and Phase < 0.91 then
            local Pose = Pulse:GetActorTransform()
            local Radius = 0.24 + Phase * 1.06
            Pose.Location = State.mBeamSpawn.Location + X * (1.4 - Phase * 8)
            Pose.Scale = Vector.new(1, Radius, Radius)
            Pose.Rotation = Quat.new(X, -Phase * 0.68)
            Pulse:SetActorTransform(Pose)
        else
            Pulse:SetActorLocation(State.mPulseStandby)
        end
    end
end

function BeginPlay()
    State.mMesh = assert(Actor:GetSkeletalMeshComponent())
    State.mSpawn = Actor:GetActorTransform()
    State.mCamera = Find("Follow Camera")
    State.mLens = assert(State.mCamera:GetCameraComponent())
    State.mCameraSettings = assert(State.mLens:GetDepthOfFieldSettings())
    State.mLabels = {
        Objective = Component(State.mCamera, "Objective"),
        Prompt = Component(State.mCamera, "Prompt"),
        Readout = Component(State.mCamera, "Readout"),
    }

    State.mRover = Find("Kestrel Rover")
    State.mRoverSpawn = State.mRover:GetActorTransform()
    State.mDrive = Component(State.mRover, "Drive")
    State.mChassis = Component(State.mRover, "Chassis Contact")

    State.mWitness = Find("Echo Witness")
    State.mWitnessMesh = assert(State.mWitness:GetSkeletalMeshComponent())
    State.mWitnessSpawn = State.mWitness:GetActorTransform()
    assert(State.mMesh:SetSkeletalMesh(EchoMesh, EchoSkeleton))
    assert(State.mWitnessMesh:SetSkeletalMesh(EchoMesh, EchoSkeleton))

    State.mDockSign = Component(Find("Relay Dock"), "Sign")
    State.mBenchSign = Component(Find("Optical Focus Bench"), "Bench Sign")
    State.mBeam = Find("Relay Beam")
    State.mBeamSpawn = State.mBeam:GetActorTransform()
    State.mPulses = {Find("Transmission Pulse 1"), Find("Transmission Pulse 2"), Find("Transmission Pulse 3")}
    State.mPulseStandby = State.mPulses[1].Location
    State.mBenchSpawn = Actor:GetActorTransform()
    State.mBenchSpawn.Location = Vector.new(40.5, 0.52, 0.025)

    State.mCargo = {}
    for Index = 1, 6 do
        local Cargo = Find("Sensor Charge " .. Index)
        State.mCargo[Index] = {mActor = Cargo, mBody = Component(Cargo, "Contact"), mSpawn = Cargo:GetActorTransform()}
    end

    for _, Surface in ipairs({{"Caldera Deck", "Deck Contact"}, {"Caldera Bed", "Bed Contact"}, {"Suspension Ramp", "Ramp Contact"}}) do
        assert(Component(Find(Surface[1]), Surface[2]):SetVehicleDrivable(true), "No PhysX surface on " .. Surface[1])
    end

    assert(World.SetActiveCamera(State.mCamera))
    ResetMission()
    WalkCamera(0)
end

function Tick(DeltaTime)
    local Step = Clamp(DeltaTime, 0, 0.05)
    if Input.WasPressed(Key.Backspace) then
        ResetMission()
    end

    if State.mPhase == "Optics" then
        UpdateOptics(Step)
        return
    end
    if State.mDriving then
        DriveRover(Step)
        return
    end

    local MouseX, MouseY = Input.GetMouseDelta()
    State.mYaw = Wrap(State.mYaw + MouseX * 0.003)
    State.mPitch = Clamp(State.mPitch - MouseY * 0.003, -0.90, 0.64)
    Walk(Step)
    UpdateArm(Step)
    UpdateAnimation(Step)
    WalkCamera(Step)

    local Location = Actor.Location
    local AtRig = Near(Location, RigPosition, 2.8)
    local AtRover = Near(Location, State.mRover.Location, 2.75)
    local AtBench = Near(Location, BenchPosition, 1.95)

    if State.mPhase == "Rig" then
        if AtRig and Input.WasPressed(Key.F) then
            FireRig()
        end

        SetReadout("01   CALIBRATE THE SHOCK RIG", AtRig and "F  FIRE THE SHOCK RIG     Q  TEST ECHO'S ARM" or "WASD  WALK TO THE AMBER SHOCK RIG     Q  TEST ARM", "RELAY OFFLINE")
    elseif State.mPhase == "Rover" then
        if State.mWitnessFlying then
            State.mWitnessAge = State.mWitnessAge + Step
        end

        if AtRover and Input.WasPressed(Key.E) then
            EnterRover()
        elseif AtRig and Input.WasPressed(Key.F) then
            FireRig()
        end

        local Prompt = AtRover and "E  BOARD KESTREL" or AtRig and (State.mWitnessFlying and "F  RESET RIG" or "F  REPLAY SHOCK") .. "     WALK TO THE ROVER" or "WALK TO THE ORANGE ROVER     E  BOARD"
        SetReadout("02   TAKE THE KESTREL ROVER", Prompt, State.mWitnessAge < 3 and State.mWitnessFlying and "CALIBRATION WITNESS RELEASED" or "RIG CALIBRATED")
    elseif State.mPhase == "Bench" then
        if AtBench and Input.WasPressed(Key.F) then
            BeginOptics()
        end

        SetReadout("03   APPROACH THE OPTICAL BENCH", AtBench and "F  LOOK THROUGH THE APERTURE" or "FOLLOW THE BLUE DECK TO THE THREE OPTICAL TARGETS", "ROVER DOCKED")
    else
        UpdateTransmission(Step)
        SetReadout("RELAY ONLINE", "MOUSE  LOOK     WASD  EXPLORE     BACKSPACE  RESTART", "SIGNAL TRANSMITTED")
    end
end
