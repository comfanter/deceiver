#pragma once

#include "data/entity.h"
#include "ai.h"
#include <bullet/src/btBulletDynamicsCommon.h>

namespace VI
{

namespace Net
{
	struct StreamRead;
	struct StateFrame;
}

struct PlayerManager;
struct Transform;
struct RigidBody;
struct PlayerControlHuman;
struct View;

void spawn_sparks(const Vec3&, const Quat&, Transform* = nullptr);

struct DroneEntity : public Entity
{
	DroneEntity(AI::Team, const Vec3&);
};

struct HealthEvent
{
	Ref<Entity> source;
	s8 hp;
	s8 shield;
};

struct Health : public ComponentType<Health>
{
	struct BufferedDamage
	{
		enum class Type : s8
		{
			Sniper,
			Other,
			count,
		};

		r32 delay;
		Ref<Entity> source;
		s8 damage;
		Type type;
	};

	static b8 net_msg(Net::StreamRead*);

	r32 active_armor_timer;
	r32 regen_timer;
	Array<BufferedDamage> damage_buffer;
	LinkArg<const HealthEvent&> changed;
	LinkArg<Entity*> killed;
	s8 shield;
	s8 shield_max;
	s8 hp;
	s8 hp_max;

	Health(s8 = 0, s8 = 0, s8 = 0, s8 = 0);

	b8 damage_buffer_required(const Entity*) const;
	void update(const Update&);
	void awake() {}
	void damage(Entity*, s8, const Net::StateFrame* = nullptr);
	void damage_force(Entity*, s8);
	void reset_hp();
	void kill(Entity*);
	void add(s8);
	s8 total() const;
	b8 active_armor(const Net::StateFrame* = nullptr) const;
	b8 can_take_damage(Entity*, const Net::StateFrame* = nullptr) const;
};

struct Shield : public ComponentType<Shield>
{
	static void update_all(const Update&);

	Ref<View> inner;
	Ref<View> outer;
	Ref<View> active_armor;

	void awake();
	~Shield();

	void update_client(const Update&);
	void health_changed(const HealthEvent&);
};

struct SpawnPoint;

struct BatteryEntity : public Entity
{
	BatteryEntity(const Vec3&, SpawnPoint*, AI::Team = AI::TeamNone);
};

struct TargetEvent
{
	Entity* hit_by;
	Entity* target;
};

struct Battery : public ComponentType<Battery>
{
	struct Comparator
	{
		Vec3 me;
		b8 closest_first;
		r32 priority(const Ref<Battery>&);
		s32 compare(const Ref<Battery>&, const Ref<Battery>&);
	};

	static r32 particle_accumulator;
	static r32 increment_timer;

	static void awake_all();
	static void update_all(const Update&);
	static void sort_all(const Vec3&, Array<Ref<Battery>>*, b8, AI::TeamMask);
	static Battery* closest(AI::TeamMask, const Vec3&, r32* = nullptr);
	static s32 count(AI::TeamMask);
	static b8 net_msg(Net::StreamRead*);

	Ref<Entity> light;
	Ref<SpawnPoint> spawn_point;
	s16 energy;
	AI::Team team = AI::TeamNone;

	void awake();
	~Battery();

	void killed(Entity*);
	void hit(const TargetEvent&);
	b8 set_team(AI::Team, Entity* = nullptr);
	void set_team_client(AI::Team);
};

struct SpawnPointEntity : public Entity
{
	SpawnPointEntity(AI::Team, b8);
};

struct SpawnPosition
{
	Vec3 pos;
	r32 angle;
};

struct SpawnPoint : public ComponentType<SpawnPoint>
{
	AI::Team team;

	void awake() {}
	void set_team(AI::Team);
	SpawnPosition spawn_position() const;
	Battery* battery() const;
};

struct Drone;
struct UpgradeStation : public ComponentType<UpgradeStation>
{
	enum class Mode : s8
	{
		Deactivating,
		Activating,
		count,
	};

	static b8 net_msg(Net::StreamRead*, Net::MessageSource);
	static UpgradeStation* drone_at(const Drone*);
	static UpgradeStation* drone_inside(const Drone*);
	static UpgradeStation* closest_available(AI::Team, const Vec3&, r32* = nullptr);

	r32 timer;
	Ref<SpawnPoint> spawn_point;
	Ref<Drone> drone;
	Mode mode;

	void awake() {}
	void update(const Update&);
	void drone_enter(Drone*);
	void drone_exit();
	void transform(Vec3*, Quat*) const;
	Quat rotation() const;
};

struct UpgradeStationEntity : public Entity
{
	UpgradeStationEntity(SpawnPoint*);
};

struct RectifierEntity : public Entity
{
	RectifierEntity(PlayerManager*, const Vec3&, const Quat&, Transform* = nullptr);
};

struct Rectifier : public ComponentType<Rectifier>
{
	static Array<InstanceVertex> heal_effects;
	static s8 heal_counts[MAX_ENTITIES];
	static Bitmask<MAX_ENTITIES> heal_targets;

	static b8 can_see(AI::TeamMask, const Vec3&, const Vec3&);
	static Rectifier* closest(AI::TeamMask, const Vec3&, r32* = nullptr);
	static void update_all(const Update&);
	static void draw_alpha_all(const RenderParams&);

	Vec3 abs_pos_attached;
	Ref<PlayerManager> owner;
	AI::Team team;

	Rectifier(AI::Team = AI::TeamNone, PlayerManager* = nullptr);
	void awake();

	void killed_by(Entity*);
};

struct Flag : public ComponentType<Flag>
{
	enum class StateChange
	{
		Scored,
		Dropped,
		PickedUp,
		Restored,
		count,
	};

	static b8 net_msg(Net::StreamRead*, Net::MessageSource);
	static Flag* for_team(AI::Team);

	Vec3 pos_cached;
	r32 timer;
	AI::Team team;
	b8 at_base;

	void awake();
	void player_entered_trigger(Entity*);
	void drop();
	void update_server(const Update&);
	void update_client(const Update&);
};

struct FlagEntity : public Entity
{
	FlagEntity(AI::Team);
};

struct Glass : public ComponentType<Glass>
{
	static b8 net_msg(Net::StreamRead*, Net::MessageSource);
	static void shatter_all(const Vec3&, const Vec3&);

	r32 client_side_shatter_timer; // if this above zero, we've been shattered locally, and we're waiting for server confirmation. if it goes to zero, we reset

	void awake() {}

	void shatter(const Vec3&, const Vec3&);

	void update_client(const Update&);
};

struct GlassEntity : public Entity
{
	GlassEntity(const Vec2&);
};

struct GlassShard
{
	static Array<GlassShard> list;
	static AssetID index_buffer;
	static AssetID index_buffer_edges;

	static GlassShard* add(const Vec2&, const Vec2&, const Vec2&, const Vec3&, const Quat&, const Vec3&, const Vec3&);
	static GlassShard* add(const Vec2&, const Vec2&, const Vec2&, const Vec2&, const Vec3&, const Quat&, const Vec3&, const Vec3&);
	static void clear();
	static void update_all(const Update&);
	static void draw_all(const RenderParams&);
	static void sync_physics();

	btCollisionShape* btShape;
	btRigidBody* btBody;
	Quat rot;
	Vec3 pos;
	r32 timestamp;
	AssetID mesh_id;

	void cleanup();
};

struct MinionSpawnerEntity : public Entity
{
	MinionSpawnerEntity(PlayerManager*, AI::Team, const Vec3&, const Quat&, Transform* = nullptr);
};

struct MinionSpawner : public ComponentType<MinionSpawner>
{
	static void update_all(const Update&);

	Vec3 abs_pos_attached;
	AI::Team team;
	Ref<PlayerManager> owner;

	void awake();
	void killed_by(Entity*);
};

struct TurretEntity : public Entity
{
	TurretEntity(PlayerManager*, AI::Team, const Vec3&, const Quat&, Transform* = nullptr);
};

struct Turret : public ComponentType<Turret>
{
	static r32 particle_accumulator;

	static void update_all(const Update&);
	static b8 net_msg(Net::StreamRead*, Net::MessageSource);

	Vec3 abs_pos_attached;
	r32 cooldown;
	r32 target_check_time;
	Ref<Entity> target;
	Ref<PlayerManager> owner;
	AI::Team team;
	b8 charging;

	void awake();
	~Turret();

	void killed(Entity*);
	void update_server(const Update&);
	void check_target();
	b8 can_see(Entity*) const;
};

struct ForceField;
struct ForceFieldCollision : public ComponentType<ForceFieldCollision>
{
	Ref<ForceField> field;
	void awake() {}
};

struct ForceField : public ComponentType<ForceField>
{
	enum class HashMode : s8
	{
		All,
		OnlyInvincible,
		count,
	};

	static r32 particle_accumulator;

	static void update_all(const Update&);
	static ForceField* inside(AI::TeamMask, const Vec3&);
	static ForceField* closest(AI::TeamMask, const Vec3&, r32*);
	static u32 hash(AI::Team, const Vec3&, HashMode = HashMode::All);
	static b8 can_spawn(AI::Team, const Vec3&, r32 = FORCE_FIELD_RADIUS);

	enum Flags
	{
		FlagPermanent = 1 << 0,
		FlagInvincible = 1 << 1,
		FlagAttached = 1 << 2,
	};

	enum class Type
	{
		Normal,
		Permanent,
		Invincible,
		count,
	};

	Vec3 abs_pos_attached;
	r32 spawn_death_timer;
	r32 damage_timer;
	Ref<ForceFieldCollision> collision;
	AI::Team team;
	s8 flags;

	ForceField();
	void awake();
	~ForceField();
	void health_changed(const HealthEvent&);
	void killed(Entity*);
	void destroy();
	b8 is_flag_force_field() const;
	b8 contains(const Vec3&) const;
	Vec3 base_pos() const;
};

struct ForceFieldEntity : public Entity
{
	ForceFieldEntity(Transform*, const Vec3&, const Quat&, AI::Team, ForceField::Type = ForceField::Type::Normal);
};

struct EffectLight
{
	enum class Type : s8
	{
		BoltDroneBolter,
		BoltDroneShotgun,
		Grenade,
		Spark,
		Shockwave,
		Explosion,
		MuzzleFlash,
		count,
	};

	static PinArray<EffectLight, MAX_ENTITIES> list;

	static EffectLight* add(const Vec3&, r32, r32, Type, Transform* = nullptr, Quat = Quat::identity);
	static void remove(EffectLight*);
	static void draw_alpha(const RenderParams&);
	static void draw_opaque(const RenderParams&);

	Quat rot;
	Vec3 pos;
	r32 max_radius;
	r32 timer;
	r32 duration;
	Ref<Transform> parent;
	Revision revision;
	Type type;

	r32 radius() const;
	r32 opacity() const;
	Vec3 absolute_pos() const;
	void update(const Update&);

	inline ID id() const
	{
		return ID(this - &list[0]);
	}
};

struct WaterEntity : public Entity
{
	WaterEntity(AssetID);
};

struct Rope : public ComponentType<Rope>
{
	static Array<InstanceVertex> instances;

	static void draw_all(const RenderParams&);
	static void spawn(const Vec3&, const Vec3&, r32, r32 = 0.0f, b8 = true, s8 = 0);
	static Rope* start(RigidBody*, const Vec3&, const Vec3&, const Quat&, r32 = 0.0f, s8 = 0);

	enum Flags
	{
		FlagClimbable = 1 << 0,
	};

	s8 flags;

	void awake() {}

	void end(const Vec3&, const Vec3&, RigidBody*, r32 = 0.0f, b8 = false);
};

struct Bolt : public ComponentType<Bolt>
{
	enum class Type : s8
	{
		Minion,
		Turret,
		DroneBolter,
		DroneShotgun,
		count,
	};

	enum class ReflectionType : s8
	{
		Homing,
		Simple,
		count,
	};

	static r32 particle_accumulator;

	struct Hit
	{
		Entity* entity;
		Vec3 point;
		Vec3 normal;
	};

	static r32 speed(Type, b8 = false);
	static s16 raycast_mask(AI::Team);
	static b8 net_msg(Net::StreamRead*, Net::MessageSource);
	static void update_client_all(const Update&);
	static b8 default_raycast_filter(Entity*, AI::Team);
	static b8 raycast(const Vec3&, const Vec3&, s16, AI::Team, Hit*, b8(*)(Entity*, AI::Team), const Net::StateFrame* = nullptr, r32 = 0.0f);
	
	Vec3 velocity;
	Vec3 last_pos;
	r32 remaining_lifetime;
	Ref<PlayerManager> player;
	Ref<Entity> owner;
	AI::Team team;
	Type type;
	b8 reflected;

	void awake();

	b8 visible() const; // bolts are invisible and essentially inert while they are waiting for damage buffer
	void reflect(const Entity*, ReflectionType = ReflectionType::Homing, const Vec3& = Vec3::zero);
	b8 simulate(r32, Hit* = nullptr, const Net::StateFrame* = nullptr); // returns true if the bolt hit something
	void hit_entity(const Hit&, const Net::StateFrame* = nullptr);
};

struct BoltEntity : public Entity
{
	BoltEntity(AI::Team, PlayerManager*, Entity*, Bolt::Type, const Vec3&, const Vec3&);
};

struct ParticleEffect
{
	enum class Type : s8
	{
		Fizzle,
		ImpactLarge,
		ImpactSmall,
		ImpactTiny,
		Explosion,
		DroneExplosion,
		Grenade,
		SpawnDrone,
		SpawnMinion,
		SpawnForceField,
		SpawnRectifier,
		SpawnMinionSpawner,
		SpawnTurret,
		count,
	};

	static Array<ParticleEffect> list;

	static b8 spawn(Type, const Vec3&, const Quat&, Transform* = nullptr, PlayerManager* = nullptr, AI::Team = AI::TeamNone);
	static b8 net_msg(Net::StreamRead*);
	static void update_all(const Update&);
	static void clear();
	static b8 is_spawn_effect(Type);
	static void draw_alpha(const RenderParams&);

	Quat rot;
	Quat abs_rot;
	Vec3 pos;
	Vec3 abs_pos;
	r32 lifetime;
	Ref<Transform> parent;
	Ref<PlayerManager> owner;
	AI::Team team;
	Type type;
};

struct GrenadeEntity : public Entity
{
	GrenadeEntity(PlayerManager*, const Vec3&, const Vec3&);
};

struct Grenade : public ComponentType<Grenade>
{
	enum class State
	{
		Inactive,
		Active,
		Attached,
		Exploded,
		count,
	};

	static r32 particle_accumulator;
	static void update_all(const Update&);
	static b8 net_msg(Net::StreamRead*, Net::MessageSource);

	Vec3 abs_pos_attached;
	Vec3 velocity;
	r32 timer;
	Ref<PlayerManager> owner;
	AI::Team team;
	State state;

	void awake();

	void hit_by(const TargetEvent&);
	void hit_entity(const Bolt::Hit&, const Net::StateFrame* = nullptr);
	void killed_by(Entity*);
	void explode();
	void set_owner(PlayerManager*);

	b8 simulate(r32, Bolt::Hit* = nullptr, const Net::StateFrame* = nullptr); // returns true if grenade hits something
};

struct Target : public ComponentType<Target>
{
	Vec3 local_offset;
	Vec3 net_velocity;
	LinkArg<const TargetEvent&> target_hit;

	void awake() {}
	Vec3 velocity() const;
	Vec3 absolute_pos() const;
	void hit(Entity*);
	b8 predict_intersection(const Vec3&, r32, const Net::StateFrame*, Vec3*) const;
	r32 radius() const;
};

struct ShellCasing
{
	enum class Type : s8
	{
		Bolter,
		Shotgun,
		Sniper,
		count,
	};

	static Array<ShellCasing> list;
	static Array<InstanceVertex> instances;

	static void spawn(const Vec3&, const Quat&, Type);
	static void clear();
	static void update_all(const Update&);
	static void draw_all(const RenderParams&);
	static void sync_physics();

	btRigidBody* btBody;
	btBoxShape* btShape;
	Quat rot;
	Vec3 pos;
	r32 timer;
	Type type;

	void cleanup();
};

struct Collectible : public ComponentType<Collectible>
{
	Resource type;
	Link collected;
	ID save_id;
	s16 amount;
	AssetID audio_log;

	void awake() {}

	void give_rewards();
};

struct CollectibleEntity : public Entity
{
	CollectibleEntity(ID, Resource = Resource::count, s16 = 0);
};

struct PlayerTrigger : public ComponentType<PlayerTrigger>
{
	const static s32 max_trigger = MAX_PLAYERS;
	r32 radius;
	LinkArg<Entity*> entered;
	LinkArg<Entity*> exited;
	Ref<Entity> triggered[max_trigger];

	PlayerTrigger();

	void awake() {}

	void update(const Update&);

	b8 is_triggered(const Entity*) const;
	b8 is_triggered() const;

	s32 count() const;
};

struct Interactable : public ComponentType<Interactable>
{
	enum Type
	{
		Terminal,
		Tram,
		Shop,
		Invalid,
		count = Invalid,
	};

	static Interactable* closest(const Vec3&);
	static b8 is_present(Type);

	s32 user_data;
	Type type;
	LinkArg<Interactable*> interacted;

	Interactable(Type = Type::Invalid);

	void awake();
	void interact();
	void interact_no_animation();
	void animation_callback();
};

struct TerminalEntity : public Entity
{
	static void open();
	static void close();
	static void closed();

	TerminalEntity();
};

struct TerminalInteractableEntity : public Entity
{
	static void interacted(Interactable*);

	TerminalInteractableEntity();
};

struct ShopEntity : public Entity
{
	ShopEntity();
};

struct ShopInteractableEntity : public Entity
{
	static void interacted(Interactable*);
	static const s32 flags_default = ~(1 << s32(Resource::Energy))
		& ~(1 << s32(Resource::AudioLog));

	ShopInteractableEntity();
};

struct TramRunnerEntity : public Entity
{
	TramRunnerEntity(s8, b8);
};

struct TramRunner : public ComponentType<TramRunner>
{
	enum class State : s8
	{
		Idle,
		Arriving,
		Departing,
		count,
	};

	static void go(s8, r32, State);

	r32 target_offset;
	r32 offset;
	r32 velocity;
	s32 offset_index;
	State state;
	s8 track;
	b8 is_front; // front is toward the exit

	void awake();

	void update(const Update&);

	void set(r32);
};

struct TramEntity : public Entity
{
	TramEntity(TramRunner*, TramRunner*);
};

struct Tram : public ComponentType<Tram>
{
	static Tram* by_track(s8);
	static Tram* player_inside(Entity*);
	static void setup();

	Ref<TramRunner> runner_a;
	Ref<TramRunner> runner_b;
	Ref<Entity> doors;
	b8 departing;
	b8 arrive_only;

	b8 doors_open() const;
	void doors_open(b8);
	s8 track() const;
	void set_position();

	void player_entered(Entity*);
	void player_exited(Entity*);

	void awake();
};

struct TramInteractableEntity : public Entity
{
	static void interacted(Interactable*);

	TramInteractableEntity(const Vec3&, const Quat&, s8);
};

struct Ascensions
{
	struct Entry
	{
		r32 timer;
		char username[MAX_USERNAME + 1];
		b8 vip;

		Vec3 pos() const;
		r32 scale() const;
	};

	static Array<Entry> list;
	static r32 timer;
	static r32 particle_accumulator;

	static void add(const char*, b8);
	static void update(const Update&);
	static void draw_ui(const RenderParams&);
	static void clear();
};

struct Asteroids
{
	struct Entry
	{
		Vec3 pos;
		Vec3 velocity;
		r32 lifetime;
	};

	static Array<Entry> list;
	static Array<InstanceVertex> instances;
	static r32 timer;
	static r32 particle_accumulator;

	static void update(const Update&);
	static void draw_alpha(const RenderParams&);
	static void clear();
};

#define TILE_SIZE 0.5f
struct Tile
{
	static PinArray<Tile, MAX_ENTITIES> list;
	static Array<InstanceVertex> instances;

	static void add(const Vec3&, const Quat&, const Vec3&, Transform*, r32 = 0.3f);
	static void draw_alpha(const RenderParams&);
	static void clear();

	Quat relative_start_rot;
	Quat relative_target_rot;
	Vec3 relative_start_pos;
	Vec3 relative_target_pos;
	r32 timer;
	r32 anim_time;
	Ref<Transform> parent;

	ID id() const
	{
		return ID(this - &list.data[0]);
	}
	r32 scale() const;
	void update(const Update&);
};

struct AirWave
{
	static PinArray<AirWave, MAX_ENTITIES> list;

	static Array<InstanceVertex> instances;

	static void add(const Vec3&, const Quat&, r32 = 0.0f);
	static void draw_alpha(const RenderParams&);
	static void clear();

	Quat rot;
	Vec3 pos;
	r32 timestamp;

	ID id() const
	{
		return ID(this - &list.data[0]);
	}

	void update(const Update&);
};


}
