# TacticalDeployment: notes for Claude Code sessions

5v5 round-based tactical FPS in C++ for **Unreal Engine 5.4+**: authoritative 128-tick dedicated server, Iris, push model, no abilities. Read `Docs/ARCHITECTURE.md` for the design; this file is the working guide.

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

## Always run before committing (seconds)
```
cmake -S Tools/Simulation -B Tools/Simulation/Build -DCMAKE_BUILD_TYPE=Release
cmake --build Tools/Simulation/Build -j
Tools/Simulation/Build/TacticalSimulation          # expect: all passed
python3 Tools/Lint/check_unreal_conventions.py     # expect: no findings
```

## Status and next steps (first local UE session)
The UE-bound code has **never been compiled against a real engine**. In order:
1. **Compile the editor target**, for example on Windows:
   `"<UE_5.4>\Engine\Build\BatchFiles\Build.bat" TacticalDeploymentEditor Win64 Development -Project="<repo>\TacticalDeployment.uproject" -WaitMutex`
   Fix every error, and re-run the simulation and checker after each round.
2. **Likely first errors:**
   - The Editor and Game targets set `bUseIris` / `bWithPushModel`. A Launcher (installed) engine may reject targets that change global build settings ("modifies the value of ..."). If so, drop those overrides from the Editor/Game targets (Iris and push model are compiled into UE 5.4 by default) and keep them only for source-engine builds.
   - Iris API names in the `#if UE_WITH_IRIS` block of `Private/Net/FogOfWarSubsystem.cpp`, and `UNetConnection::GetConnectionId`.
   - `UE_VERSION_OLDER_THAN` guards (NetUpdateFrequency setters, first-person rendering).
3. The **dedicated server target** (`TacticalDeploymentServer`) needs a **source-built** engine; Launcher builds can't build Server targets.
4. Headless runtime checks: run a dedicated server plus 2 clients with `-nullrhi` and `NetEmulation.PktLag=60 PktLagVariance=10 PktLoss=1`. Then port the simulation scenarios to automation tests (`IMPLEMENT_SIMPLE_AUTOMATION_TEST`) and profile with `stat Tactical` and Unreal Insights against the 7.8125 ms budget.
5. Create assets through Unreal Python (editor scripting): the enemy-outline Material Parameter Collection (`EnemyHighlightColor`) and post-process material (recipe in ARCHITECTURE.md section 11), `UWeaponStats` assets with recoil curves, `BP_TacticalCharacter` with the hitbox list, and a greybox test map with spawns, spike sites and barriers.
6. Calibrate `TacticalNet::ProxyInterpolationDelay` against measured proxy display lag (about +/-10 ms of margin).

## Git
The working branch is `claude/tactical-fps-ue5-architecture-pls4av`. `main` is updated only when the owner asks.
