# TacticalDeployment

C++ architecture for a 5v5, round-based tactical FPS in **Unreal Engine 5.4+**. It has no abilities: gunplay, deterministic movement, and a spike (bomb) objective.

- Authoritative **128-tick** dedicated server (7.8125 ms frame budget)
- **Iris** replication with push-model properties throughout
- Server-side **network fog of war** (anti-ESP)
- **Lag-compensated** hitscan with a 1000 ms hitbox history and exact ray-vs-capsule rewind
- **Deterministic recoil patterns**, velocity-based spread, server-seeded spread, and wallbangs driven by material density
- **Counter-strafe-friendly** movement, with tagging (slow on hit) that stays in sync with client prediction
- Round state machine with a spike that has a **50% defuse checkpoint**
- Split first-person / third-person rendering, with a 16-bit aim offset that matches the server's hitboxes
- Asset-free competitive HUD with an **honest crosshair** that opens by the real spread, plus hit markers, a kill feed and spike progress
- Procedural viewmodel sway, bob and kick on exact springs (identical feel at 30-360 FPS), and directional ragdoll deaths
- Competitive rendering and scalability profile: fixed exposure, no blur or bloom, a colour-blind-selectable enemy outline, a low-latency mode, and quality levels that never change what you can see

Read **[Docs/ARCHITECTURE.md](Docs/ARCHITECTURE.md)** for the full design, the per-pillar reasoning, the 128-tick budget and the integration checklist.

| Pillar | Key files |
|---|---|
| 1. Netcode and fog of war | `Config/DefaultEngine.ini`, `Net/FogOfWarSubsystem.*`, `ATacticalCharacter::IsNetRelevantFor` |
| 2. Lag compensation | `Combat/LagCompensationComponent.*`, `Game/TacticalPlayerController.*` (clock sync) |
| 3. Gunplay | `Weapons/WeaponStats.*`, `Weapons/TacticalWeapon.*`, `Weapons/TacticalPhysicalMaterial.h` |
| 4. Movement and tagging | `Character/TacticalCharacterMovementComponent.*`, `Character/TakeDamageTagging.h` |
| 5. Rounds and spike | `Game/TacticalGameMode.*`, `Game/TacticalGameState.*`, `Game/TacticalPlayerState.*`, `Game/SpikeBase.*` |
| 6. Camera and viewmodel | `Character/TacticalCharacter.*`, `Animation/TacticalAnimInstance.*` |

## Verification

No Unreal Engine install is available where this was built, so the game rules are verified in a headless simulation. The harness compiles the real rule headers and `WeaponStats.cpp` against a thin UE-core stand-in and plays 53 scenarios in a virtual world: corner peeks under latency, headshots on strafing targets, wallbangs, fire-rate cheats, counter-strafing, spike races and 1000 full matches. A static checker covers the UE-bound wiring (RPCs, replication registration, push-model dirtiness).

```
cmake -S Tools/Simulation -B Tools/Simulation/Build -DCMAKE_BUILD_TYPE=Release
cmake --build Tools/Simulation/Build -j && Tools/Simulation/Build/TacticalSimulation
python3 Tools/Lint/check_unreal_conventions.py
```

Results, the defects the simulation found (and which are fixed) and what it cannot cover are in [Docs/ARCHITECTURE.md, section 10](Docs/ARCHITECTURE.md#10-verification-headless-simulation).

> The UE-bound classes (actors, components, RPCs, Iris) have not been compiled against an engine install yet, and no content (meshes, Blueprints, maps) is included. See *Known gaps* in the architecture doc.
