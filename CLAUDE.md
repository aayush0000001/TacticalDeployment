# TacticalDeployment: notes for Claude Code sessions

5v5 round-based tactical FPS in C++ for **Unreal Engine 5.8**: authoritative 128-tick dedicated server, Iris, push model, no abilities. Read `Docs/ARCHITECTURE.md` for the design; this file is the working guide.

## Layout
- `Source/TacticalDeployment/Public|Private/` is one runtime module.
  - **Engine-light rules** (plain data and math on UE core types, no UObjects). The UE classes call these, and the simulation compiles them directly: `Combat/HitboxRewind.h`, `Weapons/ShotRules.h`, `Weapons/WeaponStats.*`, `Game/SpikeRules.h`, `Game/RoundRules.h`, `Net/FogOfWarRules.h`, `Core/ClockSync.h`, `Core/ViewmodelMotion.h`, `Core/HudMath.h`, `Character/TacticalMovementTuning.h`, `Character/TakeDamageTagging.h`.
  - **UE-bound classes**: character, CMC, weapon, lag-comp component, fog subsystem (Iris exclusion groups), game mode/state/player state/controller, spike, HUD, user settings.
- `Tools/Simulation/` is a headless harness (UE-core shim + scenario tests). `Tools/Lint/check_unreal_conventions.py` is a static checker for RPCs, replication registration and push-model dirtiness.
- `Config/` holds the 128 Hz and Iris setup, collision channels, the competitive rendering profile, and scalability.

## Rules for changes
- New game logic that can be pure goes in an engine-light header, with a test in `Tools/Simulation/Tests/`. The UE class only gathers inputs and applies results.
- Every replicated property is push-based (`FDoRepLifetimeParams.bIsPushBased = true`). Every write must `MARK_PROPERTY_DIRTY_FROM_NAME` (the checker enforces this).
- Server authority: clients send intent (aim, stamps, inputs), never outcomes.
- Never trust client timestamps for banking or recovery; see `ShotRules::FFireCadenceGate`.
- Targets pin `BuildSettingsVersion.V7` and `EngineIncludeOrderVersion.Unreal5_8`. Installed (Launcher/binary) engines reject Editor and Game targets that change the engine's build environment ("modifies the values of properties"), which is why the Game target does not set `bWithPushModel`.
- Include what you use: unity builds and the shared PCH hide missing includes, so check UE-bound changes with the strict build below.

## Always run before committing (seconds)
```
cmake -S Tools/Simulation -B Tools/Simulation/Build -DCMAKE_BUILD_TYPE=Release
cmake --build Tools/Simulation/Build -j
Tools/Simulation/Build/TacticalSimulation          # expect: all passed
python3 Tools/Lint/check_unreal_conventions.py     # expect: no findings
```
After changing UE-bound code, also build the editor target (about a minute when incremental; on Windows use `Build.bat` with `Win64`):
```
"<UE_5.8>/Engine/Build/BatchFiles/Linux/Build.sh" TacticalDeploymentEditor Linux Development -Project="<repo>/TacticalDeployment.uproject" -WaitMutex
```
Expect `Result: Succeeded` with no warnings. Add `-DisableUnity -NoPCH -NoSharedPCH` for the strict build.

## UE 5.8 API notes
- Iris is always compiled in and `bUseIris` no longer exists; `SetupIrisSupport` still defines `UE_WITH_IRIS=1`.
- Iris handles come from `ReplicationSystem->GetReplicationBridge()->GetReplicatedRefHandle(Object)` (`FReplicationSystemUtil` has no handle getter). Connection ids come from `NetConnection->GetConnectionHandle().GetParentConnectionId()` (`GetConnectionId` is deprecated).
- A CVar renamed by the engine still works but raises an ensure on every launch. Check new config CVars against the engine's `Config/BaseScalability.ini` and `FAutoConsoleVariableDeprecated` declarations.

## Status and next steps
The editor target compiles against UE 5.8.3 with no warnings (unity and strict builds), the module loads in a headless editor (`UnrealEditor-Cmd <uproject> -run=DumpHiddenCategories -nullrhi -unattended`), and the Game target's module compiles (`-NoLink`). Nothing has run in a networked match yet. In order:
1. The **dedicated server target** (`TacticalDeploymentServer`) needs a **source-built** engine; installed builds can't build Server targets.
2. Headless runtime checks: run a dedicated server plus 2 clients with `-nullrhi` and `NetEmulation.PktLag=60 PktLagVariance=10 PktLoss=1`. Then port the simulation scenarios to automation tests (`IMPLEMENT_SIMPLE_AUTOMATION_TEST`) and profile with `stat Tactical` and Unreal Insights against the 7.8125 ms budget.
3. Create assets through Unreal Python (editor scripting): the enemy-outline Material Parameter Collection (`EnemyHighlightColor`) and post-process material (recipe in ARCHITECTURE.md section 11), `UWeaponStats` assets with recoil curves, `BP_TacticalCharacter` with the hitbox list, and a greybox test map with spawns, spike sites and barriers.
4. Calibrate `TacticalNet::ProxyInterpolationDelay` against measured proxy display lag (about +/-10 ms of margin).

## Git
The working branch is `claude/tactical-fps-ue5-architecture-pls4av`. `main` is updated only when the owner asks.
