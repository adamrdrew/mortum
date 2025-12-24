#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "assets/asset_paths.h"
#include "assets/map_loader.h"
#include "core/base.h"
#include "game/ammo.h"
#include "game/physics_body.h"

#include "render/camera.h"
#include "render/framebuffer.h"
#include "render/texture.h"

// NOTE: This is the initial entity system implementation (slice 1: pickups).
// It is intentionally small but built around stable handles and deferred destruction.

typedef struct EntityId {
	uint32_t index;
	uint32_t gen;
} EntityId;

static inline EntityId entity_id_none(void) {
	EntityId id;
	id.index = UINT32_MAX;
	id.gen = 0;
	return id;
}

static inline bool entity_id_is_none(EntityId id) {
	return id.index == UINT32_MAX;
}

typedef enum EntityState {
	ENTITY_STATE_SPAWNING = 0,
	ENTITY_STATE_IDLE = 1,
	ENTITY_STATE_ENGAGED = 2,
	ENTITY_STATE_ATTACK = 3,
	ENTITY_STATE_DAMAGED = 4,
	ENTITY_STATE_DYING = 5,
	ENTITY_STATE_DEAD = 6,
} EntityState;

typedef enum EntityKind {
	ENTITY_KIND_INVALID = 0,
	ENTITY_KIND_PICKUP = 1,
	ENTITY_KIND_PROJECTILE = 2,
	ENTITY_KIND_TURRET = 3,
	ENTITY_KIND_ENEMY = 4,
	ENTITY_KIND_SUPPORT = 5,
} EntityKind;

typedef enum PickupType {
	PICKUP_TYPE_HEALTH = 0,
	PICKUP_TYPE_AMMO = 1,
} PickupType;

typedef struct EntityDefPickup {
	PickupType type;
	// Health pickup payload
	int heal_amount;
	// Ammo pickup payload
	AmmoType ammo_type;
	int ammo_amount;

	float trigger_radius;
	char pickup_sound[64];
	float pickup_sound_gain;
} EntityDefPickup;

typedef enum EntityEventType {
	ENTITY_EVENT_NONE = 0,
	ENTITY_EVENT_PLAYER_TOUCH = 1,
	ENTITY_EVENT_PROJECTILE_HIT_WALL = 2,
	ENTITY_EVENT_DAMAGE = 3,
	ENTITY_EVENT_DIED = 4,
	ENTITY_EVENT_PLAYER_DAMAGE = 5,
} EntityEventType;

typedef struct EntityEvent {
	EntityEventType type;
	EntityId entity;
	EntityId other;
	uint16_t def_id;
	EntityKind kind;
	float x;
	float y;
	int amount;
} EntityEvent;

typedef struct EntityDefProjectile {
	float speed;
	float lifetime_s;
	int damage;
	char impact_sound[64];
	float impact_sound_gain;
} EntityDefProjectile;

typedef struct EntityDefEnemyAnim {
	int start;
	int count;
	float fps;
} EntityDefEnemyAnim;

typedef struct EntityDefEnemy {
	float move_speed;
	float engage_range;
	float disengage_range;
	float attack_range;
	float attack_windup_s;
	float attack_cooldown_s;
	int attack_damage;
	float damaged_time_s;
	float dying_time_s;
	float dead_time_s;

	EntityDefEnemyAnim anim_idle;
	EntityDefEnemyAnim anim_engaged;
	EntityDefEnemyAnim anim_attack;
	EntityDefEnemyAnim anim_damaged;
	EntityDefEnemyAnim anim_dying;
	EntityDefEnemyAnim anim_dead;
} EntityDefEnemy;

typedef struct EntitySpriteFile {
	char name[64];
	int width;
	int height;
} EntitySpriteFile;

typedef struct EntitySpriteFrames {
	int count;
	int width;
	int height;
} EntitySpriteFrames;

typedef struct EntitySprite {
	EntitySpriteFile file;
	EntitySpriteFrames frames;
	float scale;
	float z_offset; // sprite-space pixels above floor; converted to world units using 64px == 1 world unit
} EntitySprite;

typedef struct EntityDef {
	char name[64];
	EntitySprite sprite;
	EntityKind kind;
	float radius;
	float height;
	int max_hp;

	union {
		EntityDefPickup pickup;
		EntityDefProjectile projectile;
		EntityDefEnemy enemy;
	} u;
} EntityDef;

typedef struct EntityDefs {
	EntityDef* defs; // owned
	uint32_t count;
	uint32_t capacity;
} EntityDefs;

void entity_defs_init(EntityDefs* defs);
void entity_defs_destroy(EntityDefs* defs);

// Loads entity definitions from Assets/Entities/entities.json.
// Returns false on hard failure (parse/IO). On failure, defs are left empty.
bool entity_defs_load(EntityDefs* defs, const AssetPaths* paths);

// Returns UINT32_MAX if not found.
uint32_t entity_defs_find(const EntityDefs* defs, const char* name);

typedef struct Entity {
	EntityId id;
	uint16_t def_id;
	EntityState state;
	float state_time;
	PhysicsBody body;
	float yaw_deg;
	uint16_t sprite_frame;
	int hp;
	EntityId target;
	EntityId owner;
	bool attack_has_hit;

	bool pending_despawn;
} Entity;

typedef struct EntitySystem {
	Entity* entities; // owned, size=capacity
	uint32_t* generation; // owned
	uint32_t* free_next; // owned
	uint8_t* alive; // owned
	uint32_t capacity;
	uint32_t alive_count;
	uint32_t free_head;

	// Events generated during tick; cleared each tick.
	EntityEvent* events; // owned
	uint32_t event_count;
	uint32_t event_cap;

	// Deferred despawn (slice 1 keeps this simple).
	EntityId* despawn_queue; // owned
	uint32_t despawn_count;
	uint32_t despawn_cap;

	// Spatial acceleration (deterministic spatial hash rebuilt each tick).
	float spatial_cell_size;
	uint32_t spatial_bucket_count;
	uint32_t* spatial_head; // owned, size=bucket_count, stores entity indices
	uint32_t* spatial_next; // owned, size=capacity, next index in bucket
	uint32_t* spatial_seen; // owned, size=capacity, stamp-per-query to avoid duplicates
	uint32_t spatial_stamp;
	bool spatial_valid;

	const World* world; // not owned
	const EntityDefs* defs; // not owned
} EntitySystem;

void entity_system_init(EntitySystem* es, uint32_t max_entities);
void entity_system_shutdown(EntitySystem* es);

// Resets for a new level (clears all entities).
void entity_system_reset(EntitySystem* es, const World* world, const EntityDefs* defs);

bool entity_system_spawn(EntitySystem* es, uint32_t def_index, float x, float y, float yaw_deg, int sector, EntityId* out_id);
void entity_system_request_despawn(EntitySystem* es, EntityId id);

bool entity_system_resolve(EntitySystem* es, EntityId id, Entity** out);

// Spawns all map-authored entities (placements are typically provided by map_load).
void entity_system_spawn_map(EntitySystem* es, const MapEntityPlacement* placements, int placement_count);

// Tick: advances entity logic and generates events (e.g. player touch).
// The caller is responsible for applying game-side effects (health/ammo, sounds) and then flushing despawns.
void entity_system_tick(EntitySystem* es, const PhysicsBody* player_body, float dt_s);

// Resolves player-vs-enemy overlap in the XY plane.
// This is needed because the player is not part of the entity system, but should still
// collide with enemies (prevents walking through them and avoids extreme close-range
// sprite projection artifacts).
// Deterministic: iterates entities in index order with a bounded solve iteration count.
void entity_system_resolve_player_collisions(EntitySystem* es, PhysicsBody* player_body);

// Allows the caller (e.g. main loop) to append events deterministically during effect application.
// Returns false if the event buffer is full.
bool entity_system_emit_event(EntitySystem* es, EntityEvent ev);

// Flushes deferred despawns requested via entity_system_request_despawn.
void entity_system_flush(EntitySystem* es);

// Access events generated during the last tick.
const EntityEvent* entity_system_events(const EntitySystem* es, uint32_t* out_count);

// Query entities within a 2D radius (x,y) using the spatial hash.
// Note: the spatial index is rebuilt during entity_system_tick(). If called outside tick,
// the function will rebuild lazily from current positions.
uint32_t entity_system_query_circle(EntitySystem* es, float x, float y, float radius, EntityId* out_ids, uint32_t out_cap);

// Renders billboard sprites for entities with def.sprite set.
// Uses wall_depth for occlusion against walls.
void entity_system_draw_sprites(const EntitySystem* es, Framebuffer* fb, const World* world, const Camera* cam, int start_sector, TextureRegistry* texreg, const AssetPaths* paths, const float* wall_depth);

uint32_t entity_system_alive_count(const EntitySystem* es);

