local Up = Vector.new(0, 0, 1)
local Zero = Vector.new(0, 0, 0)
local ChaseOffset = Vector.new(-8.2, 0, 3.7)
local GateCount = 6
local GateHalfWidth = 7.5
local CrateCount = 12
local LapCount = 3

local State = {}

local function Find(Name)
    return assert(World.FindActor(Name), "Missing circuit actor: " .. Name)
end

local function Text(Owner, Name)
    return assert(Owner:GetComponentByName(Name), "Missing circuit text: " .. Name)
end

local function ResetRun()
    assert(Actor:SetActorTransform(State.Spawn))
    assert(State.Movement:ResetDrive())
    State.Movement:SetDriveInput(0, 0, 0)
    State.Previous = Actor.Location

    for _, Crate in ipairs(State.Crates) do
        assert(Crate.Actor:SetActorTransform(Crate.Home))
        assert(Crate.Mesh:SetPhysicsVelocity(Zero, Zero))
    end
    State.NextGate = 1
    State.Lap = 1
    State.Elapsed = 0
    State.Finished = false
end

local function Dot(A, B)
    return A.x * B.x + A.y * B.y + A.z * B.z
end

local function CrossedGate(Gate, Before, After)
    local Start = Before - Gate.Location
    local End = After - Gate.Location
    local From = Dot(Start, Gate.Forward)
    local To = Dot(End, Gate.Forward)
    if From >= 0 or To < 0 then
        return false
    end

    -- 프레임 사이에 차량이 통과한 지점에서 폭과 높이를 검사합니다.
    local Crossing = Start + (End - Start) * (-From / (To - From))
    return math.abs(Dot(Crossing, Gate.Right)) <= GateHalfWidth
        and Crossing.z >= -0.5 and Crossing.z <= 9
end

local function UpdateRace(DeltaTime)
    if Input.WasPressed(Key.R) or Actor.Location.z < -3 then
        ResetRun()
        return
    end

    local Position = Actor.Location
    local Gate = State.Gates[State.NextGate]
    State.Elapsed = State.Elapsed + DeltaTime
    if CrossedGate(Gate, State.Previous, Position) then
        State.NextGate = State.NextGate + 1
        if State.NextGate > #State.Gates then
            if State.Lap == LapCount then
                State.Finished = true
                State.Movement:SetDriveInput(0, 1, 0)
                return
            end

            State.Lap = State.Lap + 1
            State.NextGate = 1
        end
    end
    State.Previous = Position

    local Throttle = (Input.IsDown(Key.W) and 1 or 0) - (Input.IsDown(Key.S) and 1 or 0)
    local Steering = (Input.IsDown(Key.D) and 1 or 0) - (Input.IsDown(Key.A) and 1 or 0)
    local Brake = Input.IsDown(Key.Space) and 1 or 0
    State.Movement:SetDriveInput(Brake > 0 and 0 or Throttle, Brake, Steering)
end

local function UpdateCamera()
    local Pose = Actor:GetActorTransform()
    local Heading = Quat.new(Up, math.rad(Pose:GetRotator().Yaw))
    local Eye = Pose.Location + Heading:RotateVector(ChaseOffset)
    local Target = Pose.Location + Heading:RotateVector(Vector.new(2.4, 0, 0.35))

    local Direction = Target - Eye
    local Horizontal = math.sqrt(Direction.x * Direction.x + Direction.y * Direction.y)
    local View = State.Camera:GetActorTransform()
    View.Location = Eye
    View:SetRotator(Rotator.new(math.deg(math.atan2(Direction.z, Horizontal)),
        math.deg(math.atan2(Direction.y, Direction.x)), 0))
    State.Camera:SetActorTransform(View)
    return View
end

local function TimeLabel(Seconds)
    local Centiseconds = math.floor(Seconds * 100)
    return string.format("%02d:%02d.%02d", math.floor(Centiseconds / 6000),
        math.floor(Centiseconds / 100) % 60, Centiseconds % 100)
end

local function UpdateDisplay(View)
    local Overlay = State.Finished and State.Result or State.Hud
    if State.VisibleOverlay and State.VisibleOverlay ~= Overlay then
        State.VisibleOverlay.Actor:SetActorTransform(State.VisibleOverlay.Home)
    end
    State.VisibleOverlay = Overlay
    Overlay.Actor:SetActorTransform(View)

    if not State.Finished then
        local _, Speed = State.Movement:GetDriveState()
        State.Speed:SetText(string.format("%d km/h", math.floor(math.abs(Speed) * 3.6 + 0.5)))
        State.Timer:SetText(TimeLabel(State.Elapsed))
        State.LapLabel:SetText(string.format("LAP  %d / %d", State.Lap, LapCount))
        local LineLabel = State.Lap == LapCount and "FINISH AHEAD" or "LAP LINE AHEAD"
        State.Progress:SetText(State.NextGate <= GateCount
            and string.format("CHECKPOINT  %d / %d", State.NextGate - 1, GateCount) or LineLabel)
    else
        State.ResultTime:SetText(TimeLabel(State.Elapsed))
    end
end

function BeginPlay()
    State = { Gates = {}, Crates = {} }
    State.Movement = assert(Actor:GetComponentByClass("UVehicleMovementComponent4W"))
    State.Camera = Find("Circuit Camera")
    State.Spawn = Actor:GetActorTransform()
    assert(World.SetActiveCamera(State.Camera))
    assert(Script:SetTickGroup(TickGroup.PostPhysics))

    for _, Name in ipairs({ "Circuit Ground", "Jump Ramp 01", "Jump Ramp 02" }) do
        local Surface = assert(Find(Name):GetComponentByClass("UStaticMeshComponent"))
        assert(Surface:SetVehicleDrivable(true), Name .. " requires a physics body")
    end

    for Index = 1, GateCount + 1 do
        local Name = Index <= GateCount and string.format("Checkpoint %02d", Index) or "Start and Finish"
        local Gate = Find(Name)
        local Pose = Gate:GetActorTransform()
        State.Gates[Index] = {
            -- 임시 Transform의 멤버 참조 대신 액터가 반환하는 위치 사본을 보관합니다.
            Location = Gate.Location,
            Forward = Pose.Rotation:RotateVector(Vector.new(1, 0, 0)),
            Right = Pose.Rotation:RotateVector(Vector.new(0, 1, 0)),
        }
    end

    for Index = 1, CrateCount do
        local Crate = Find(string.format("Impact Crate %02d", Index))
        State.Crates[Index] = {
            Actor = Crate,
            Mesh = assert(Crate:GetComponentByClass("UStaticMeshComponent")),
            Home = Crate:GetActorTransform(),
        }
    end

    local Hud = Find("Race HUD")
    local Result = Find("Lap Result")
    State.Hud = { Actor = Hud, Home = Hud:GetActorTransform() }
    State.Result = { Actor = Result, Home = Result:GetActorTransform() }
    State.Speed = Text(Hud, "Speed")
    State.Timer = Text(Hud, "Timer")
    State.Progress = Text(Hud, "Progress")
    State.LapLabel = Text(Hud, "Lap")
    State.ResultTime = Text(Result, "Lap time")

    ResetRun()
    UpdateDisplay(UpdateCamera())
end

function Tick(DeltaTime)
    if not State.Finished then
        UpdateRace(DeltaTime)
    elseif Input.WasPressed(Key.R) then
        ResetRun()
    end

    UpdateDisplay(UpdateCamera())
end
