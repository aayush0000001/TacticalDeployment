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

Read **[Docs/ARCHITECTURE.md](Docs/ARCHITECTURE.md)** for the full design, the per-pillar reasoning, the 128-tick budget and the integration checklist.

| Pillar | Key files |
|---|---|
| 1. Netcode and fog of war | `Config/DefaultEngine.ini`, `Net/FogOfWarSubsystem.*`, `ATacticalCharacter::IsNetRelevantFor` |
| 2. Lag compensation | `Combat/LagCompensationComponent.*`, `Game/TacticalPlayerController.*` (clock sync) |
| 3. Gunplay | `Weapons/WeaponStats.*`, `Weapons/TacticalWeapon.*`, `Weapons/TacticalPhysicalMaterial.h` |
| 4. Movement and tagging | `Character/TacticalCharacterMovementComponent.*`, `Character/TakeDamageTagging.h` |
| 5. Rounds and spike | `Game/TacticalGameMode.*`, `Game/TacticalGameState.*`, `Game/TacticalPlayerState.*`, `Game/SpikeBase.*` |
| 6. Camera and viewmodel | `Character/TacticalCharacter.*`, `Animation/TacticalAnimInstance.*` |

> The code has not been compiled against an engine install in this repository yet, and no content (meshes, Blueprints, maps) is included. See *Known gaps* in the architecture doc.
