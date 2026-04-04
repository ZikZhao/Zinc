// Polymorphism stress sandbox with optional SDL visualization.
#include <SDL2/SDL.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <random>
#include <string_view>
#include <utility>
#include <vector>

class World;

enum class EntityType {
    VoidCell,
    Wall,
    Sand,
    Water,
    Plant,
    Lava,
    Steam,
    Acid,
    Generator,
    BlackHole,
};

enum class SpawnKind {
    Water,
    Lava,
    Acid,
};

struct MoveIntent {
    int dx;
    int dy;
};

class Entity {
public:
    ~Entity() = default;

    uint64_t last_update_frame = 0;

    virtual EntityType type() const = 0;
    virtual SDL_Color color() const = 0;
    virtual bool is_static() const { return false; }

    virtual void tick(World& world, int x, int y, uint64_t frame, std::mt19937_64& rng) = 0;

    virtual bool accept_move(
        World& world, int self_x, int self_y, int from_x, int from_y, Entity& mover, uint64_t frame
    ) {
        (void)world;
        (void)self_x;
        (void)self_y;
        (void)from_x;
        (void)from_y;
        (void)mover;
        (void)frame;
        return false;
    }
};

class VoidCell;
class Wall;
class Sand;
class Water;
class Plant;
class Lava;
class Steam;
class Acid;
class Generator;
class BlackHole;

struct EntityRecipe {
    EntityType type;
    SpawnKind spawn_kind = SpawnKind::Water;
    int interval = 8;
    int steam_life = 160;
};

EntityRecipe make_entity(EntityType t);
EntityRecipe make_generator(SpawnKind kind, int interval);

size_t entity_storage_size();
size_t entity_storage_align();

class World {
public:
    World(int w, int h)
        : width_(w),
          height_(h),
          cell_size_(entity_storage_size()),
          cell_align_(entity_storage_align()),
          cell_stride_(align_up(cell_size_, cell_align_)) {
        const size_t count = static_cast<size_t>(width_) * static_cast<size_t>(height_);
        const size_t bytes = count * cell_stride_;

        cells_.resize(count);
        storage_.resize(bytes);
        for (size_t i = 0; i < count; ++i) {
            void* slot = slot_ptr(i);
            construct_from_recipe(slot, make_entity(EntityType::VoidCell));
            cells_[i] = static_cast<Entity*>(slot);
        }
    }

    ~World() {
        const size_t count = static_cast<size_t>(width_) * static_cast<size_t>(height_);
        for (size_t i = 0; i < count; ++i) {
            destroy_entity(*static_cast<Entity*>(slot_ptr(i)));
        }
    }

    World(const World&) = delete;
    World& operator=(const World&) = delete;

    int width() const { return width_; }
    int height() const { return height_; }

    bool in_bounds(int x, int y) const { return x >= 0 && x < width_ && y >= 0 && y < height_; }

    Entity& at(int x, int y) { return *cells_[index(x, y)]; }
    const Entity& at(int x, int y) const { return *cells_[index(x, y)]; }

    void set(int x, int y, EntityRecipe recipe) {
        const size_t i = index(x, y);
        Entity& cur = *cells_[i];
        destroy_entity(cur);
        construct_from_recipe(cells_[i], recipe);
    }

    void swap_cells(int x1, int y1, int x2, int y2) {
        const size_t a = index(x1, y1);
        const size_t b = index(x2, y2);
        if (a == b) {
            return;
        }
        std::swap(cells_[a], cells_[b]);
    }

    bool try_move(int from_x, int from_y, int to_x, int to_y, uint64_t frame) {
        if (!in_bounds(to_x, to_y)) {
            return false;
        }
        Entity& mover = at(from_x, from_y);
        Entity& target = at(to_x, to_y);
        return target.accept_move(*this, to_x, to_y, from_x, from_y, mover, frame);
    }

    bool try_moves(
        int x, int y, std::vector<MoveIntent> intents, uint64_t frame, std::mt19937_64& rng
    ) {
        std::shuffle(intents.begin(), intents.end(), rng);
        for (const auto& mv : intents) {
            if (try_move(x, y, x + mv.dx, y + mv.dy, frame)) {
                return true;
            }
        }
        return false;
    }

    int count_type(EntityType t) const {
        int count = 0;
        const size_t total = static_cast<size_t>(width_) * static_cast<size_t>(height_);
        for (size_t i = 0; i < total; ++i) {
            const Entity& e = *cells_[i];
            if (e.type() == t) {
                ++count;
            }
        }
        return count;
    }

    void step(uint64_t frame, std::mt19937_64& rng) {
        const int mode = static_cast<int>(frame % 4ULL);
        if (mode == 0) {
            iterate(0, height_, 1, 0, width_, 1, frame, rng);
        } else if (mode == 1) {
            iterate(height_ - 1, -1, -1, 0, width_, 1, frame, rng);
        } else if (mode == 2) {
            iterate(0, height_, 1, width_ - 1, -1, -1, frame, rng);
        } else {
            iterate(height_ - 1, -1, -1, width_ - 1, -1, -1, frame, rng);
        }
    }

private:
    static size_t align_up(size_t n, size_t a) { return (n + a - 1) / a * a; }

    void* slot_ptr(size_t i) {
        auto* base = storage_.data();
        return static_cast<void*>(base + i * cell_stride_);
    }

    const void* slot_ptr(size_t i) const {
        auto* base = storage_.data();
        return static_cast<const void*>(base + i * cell_stride_);
    }

    static void construct_from_recipe(void* dst, const EntityRecipe& recipe);
    static void destroy_entity(Entity& entity);

    size_t index(int x, int y) const {
        return static_cast<size_t>(y) * static_cast<size_t>(width_) + static_cast<size_t>(x);
    }

    void iterate(
        int y_begin,
        int y_end,
        int y_step,
        int x_begin,
        int x_end,
        int x_step,
        uint64_t frame,
        std::mt19937_64& rng
    ) {
        for (int y = y_begin; y != y_end; y += y_step) {
            for (int x = x_begin; x != x_end; x += x_step) {
                Entity& e = at(x, y);
                if (e.last_update_frame == frame) {
                    continue;
                }
                e.last_update_frame = frame;
                e.tick(*this, x, y, frame, rng);
            }
        }
    }

    int width_;
    int height_;
    size_t cell_size_;
    size_t cell_align_;
    size_t cell_stride_;
    std::vector<Entity*> cells_;
    std::vector<std::byte> storage_;
};

class VoidCell final : public Entity {
public:
    EntityType type() const override { return EntityType::VoidCell; }
    SDL_Color color() const override { return SDL_Color{20, 20, 28, 255}; }
    bool is_static() const override { return true; }

    void tick(World& world, int x, int y, uint64_t frame, std::mt19937_64& rng) override {
        (void)world;
        (void)x;
        (void)y;
        (void)frame;
        (void)rng;
    }

    bool accept_move(
        World& world, int self_x, int self_y, int from_x, int from_y, Entity& mover, uint64_t frame
    ) override {
        (void)mover;
        world.swap_cells(self_x, self_y, from_x, from_y);
        world.at(self_x, self_y).last_update_frame = frame;
        return true;
    }
};

class Wall final : public Entity {
public:
    EntityType type() const override { return EntityType::Wall; }
    SDL_Color color() const override {
        const uint8_t glow = static_cast<uint8_t>(std::min(255, 85 + static_cast<int>(heat_)));
        return SDL_Color{glow, glow, glow, 255};
    }
    bool is_static() const override { return true; }

    void tick(World& world, int x, int y, uint64_t frame, std::mt19937_64& rng) override {
        (void)rng;

        bool near_lava = false;
        static const std::array<std::pair<int, int>, 4> neighbors = {
            std::pair{1, 0},
            std::pair{-1, 0},
            std::pair{0, 1},
            std::pair{0, -1},
        };
        for (const auto& [dx, dy] : neighbors) {
            const int nx = x + dx;
            const int ny = y + dy;
            if (!world.in_bounds(nx, ny)) {
                continue;
            }
            if (world.at(nx, ny).type() == EntityType::Lava) {
                near_lava = true;
                break;
            }
        }

        if (near_lava) {
            heat_ = static_cast<uint8_t>(std::min(255, static_cast<int>(heat_) + 1));
        } else if (heat_ > 0) {
            heat_ = static_cast<uint8_t>(heat_ - 1);
        }

        if (heat_ >= 10) {
            world.set(x, y, make_entity(EntityType::Lava));
            world.at(x, y).last_update_frame = frame;
        }
    }

private:
    uint8_t heat_ = 0;
};

class Sand final : public Entity {
public:
    EntityType type() const override { return EntityType::Sand; }
    SDL_Color color() const override { return SDL_Color{219, 186, 92, 255}; }

    void tick(World& world, int x, int y, uint64_t frame, std::mt19937_64& rng) override {
        const std::vector<MoveIntent> dirs = {
            {0, 1},
            {-1, 1},
            {1, 1},
        };
        world.try_moves(x, y, dirs, frame, rng);
    }
};

class Water final : public Entity {
public:
    EntityType type() const override { return EntityType::Water; }
    SDL_Color color() const override { return SDL_Color{56, 119, 224, 255}; }

    void tick(World& world, int x, int y, uint64_t frame, std::mt19937_64& rng) override {
        if (world.try_move(x, y, x, y + 1, frame)) {
            lateral_momentum_ = 0;
            return;
        }

        std::uniform_real_distribution<double> prob(0.0, 1.0);
        if (lateral_momentum_ == 0) {
            std::uniform_int_distribution<int> side_pick(0, 1);
            lateral_momentum_ =
                (side_pick(rng) == 0) ? static_cast<int8_t>(-1) : static_cast<int8_t>(1);
        }

        const int m = static_cast<int>(lateral_momentum_);
        if (world.try_move(x, y, x + m, y + 1, frame) || world.try_move(x, y, x + m, y, frame)) {
            return;
        }

        lateral_momentum_ = static_cast<int8_t>(-m);
        if (world.try_move(x, y, x - m, y + 1, frame) || world.try_move(x, y, x - m, y, frame)) {
            return;
        }

        if (prob(rng) < 0.45) {
            lateral_momentum_ = 0;
        }
    }

    bool accept_move(
        World& world, int self_x, int self_y, int from_x, int from_y, Entity& mover, uint64_t frame
    ) override {
        if (mover.type() == EntityType::Sand || mover.type() == EntityType::Acid) {
            world.swap_cells(self_x, self_y, from_x, from_y);
            world.at(self_x, self_y).last_update_frame = frame;
            return true;
        }
        return false;
    }

private:
    int8_t lateral_momentum_ = 0;
};

class Lava final : public Entity {
public:
    EntityType type() const override { return EntityType::Lava; }
    SDL_Color color() const override { return SDL_Color{230, 88, 35, 255}; }

    void tick(World& world, int x, int y, uint64_t frame, std::mt19937_64& rng) override {
        std::uniform_real_distribution<double> prob(0.0, 1.0);

        // Active thermal reactions: sustain high-entropy transitions around lava.
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                if (dx == 0 && dy == 0) {
                    continue;
                }
                const int nx = x + dx;
                const int ny = y + dy;
                if (!world.in_bounds(nx, ny)) {
                    continue;
                }
                const EntityType t = world.at(nx, ny).type();
                if (t == EntityType::Water && prob(rng) < 0.7) {
                    world.set(nx, ny, make_entity(EntityType::Steam));
                    world.set(x, y, make_entity(EntityType::Wall));
                    world.at(nx, ny).last_update_frame = frame;
                    world.at(x, y).last_update_frame = frame;
                    return;
                }
                if (t == EntityType::Plant && prob(rng) < 0.5) {
                    world.set(nx, ny, make_entity(EntityType::VoidCell));
                    world.at(nx, ny).last_update_frame = frame;
                }
            }
        }

        if ((frame & 1ULL) == 0ULL) {
            return;
        }
        const std::vector<MoveIntent> dirs = {
            {0, 1},
            {-1, 1},
            {1, 1},
            {-1, 0},
            {1, 0},
        };
        world.try_moves(x, y, dirs, frame, rng);
    }

    bool accept_move(
        World& world, int self_x, int self_y, int from_x, int from_y, Entity& mover, uint64_t frame
    ) override {
        if (mover.type() == EntityType::Water) {
            world.set(self_x, self_y, make_entity(EntityType::Steam));
            world.set(from_x, from_y, make_entity(EntityType::Wall));
            world.at(self_x, self_y).last_update_frame = frame;
            world.at(from_x, from_y).last_update_frame = frame;
            return true;
        }
        return false;
    }
};

class Steam final : public Entity {
public:
    explicit Steam(int life = 160) : life_(life) {}

    EntityType type() const override { return EntityType::Steam; }
    SDL_Color color() const override { return SDL_Color{204, 214, 235, 255}; }

    void tick(World& world, int x, int y, uint64_t frame, std::mt19937_64& rng) override {
        --life_;
        if (life_ <= 0) {
            world.set(x, y, make_entity(EntityType::Water));
            world.at(x, y).last_update_frame = frame;
            return;
        }
        const std::vector<MoveIntent> dirs = {
            {0, -1},
            {-1, -1},
            {1, -1},
            {-1, 0},
            {1, 0},
        };
        world.try_moves(x, y, dirs, frame, rng);
    }

private:
    int life_;
};

class Plant final : public Entity {
public:
    EntityType type() const override { return EntityType::Plant; }
    SDL_Color color() const override { return SDL_Color{43, 176, 84, 255}; }
    bool is_static() const override { return true; }

    void tick(World& world, int x, int y, uint64_t frame, std::mt19937_64& rng) override {
        std::uniform_real_distribution<double> dist01(0.0, 1.0);
        bool has_water = false;

        std::vector<std::pair<int, int>> empty_neighbors;
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                if (dx == 0 && dy == 0) {
                    continue;
                }
                const int nx = x + dx;
                const int ny = y + dy;
                if (!world.in_bounds(nx, ny)) {
                    continue;
                }
                EntityType t = world.at(nx, ny).type();
                if (t == EntityType::Water) {
                    has_water = true;
                    world.set(nx, ny, make_entity(EntityType::VoidCell));
                    world.at(nx, ny).last_update_frame = frame;
                } else if (t == EntityType::VoidCell) {
                    empty_neighbors.emplace_back(nx, ny);
                }
            }
        }

        if (has_water && !empty_neighbors.empty() && dist01(rng) < 0.22) {
            std::uniform_int_distribution<size_t> pick(0, empty_neighbors.size() - 1);
            auto [gx, gy] = empty_neighbors[pick(rng)];
            world.set(gx, gy, make_entity(EntityType::Plant));
            world.at(gx, gy).last_update_frame = frame;
        }
    }
};

class Acid final : public Entity {
public:
    EntityType type() const override { return EntityType::Acid; }
    SDL_Color color() const override { return SDL_Color{175, 235, 74, 255}; }

    void tick(World& world, int x, int y, uint64_t frame, std::mt19937_64& rng) override {
        std::uniform_real_distribution<double> dist01(0.0, 1.0);

        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                if (dx == 0 && dy == 0) {
                    continue;
                }
                const int nx = x + dx;
                const int ny = y + dy;
                if (!world.in_bounds(nx, ny)) {
                    continue;
                }
                EntityType t = world.at(nx, ny).type();
                if (t == EntityType::Wall && dist01(rng) < 0.18) {
                    world.set(nx, ny, make_entity(EntityType::VoidCell));
                    world.set(x, y, make_entity(EntityType::VoidCell));
                    world.at(nx, ny).last_update_frame = frame;
                    world.at(x, y).last_update_frame = frame;
                    return;
                }
                if (t == EntityType::Plant && dist01(rng) < 0.55) {
                    world.set(nx, ny, make_entity(EntityType::VoidCell));
                    world.set(x, y, make_entity(EntityType::VoidCell));
                    world.at(nx, ny).last_update_frame = frame;
                    world.at(x, y).last_update_frame = frame;
                    return;
                }
            }
        }

        if (world.try_move(x, y, x, y + 1, frame)) {
            lateral_momentum_ = 0;
            return;
        }

        if (lateral_momentum_ == 0) {
            std::uniform_int_distribution<int> side_pick(0, 1);
            lateral_momentum_ =
                (side_pick(rng) == 0) ? static_cast<int8_t>(-1) : static_cast<int8_t>(1);
        }

        const int m = static_cast<int>(lateral_momentum_);
        if (world.try_move(x, y, x + m, y + 1, frame) || world.try_move(x, y, x + m, y, frame)) {
            return;
        }

        lateral_momentum_ = static_cast<int8_t>(-m);
        if (world.try_move(x, y, x - m, y + 1, frame) || world.try_move(x, y, x - m, y, frame)) {
            return;
        }

        if (dist01(rng) < 0.35) {
            lateral_momentum_ = 0;
        }
    }

private:
    int8_t lateral_momentum_ = 0;
};

class Generator final : public Entity {
public:
    Generator(SpawnKind spawn, int interval)
        : spawn_(spawn), interval_(std::max(1, interval)), move_interval_(6), vx_(1), vy_(0) {}

    EntityType type() const override { return EntityType::Generator; }
    SDL_Color color() const override {
        if (spawn_ == SpawnKind::Water) {
            return SDL_Color{58, 152, 255, 255};
        }
        if (spawn_ == SpawnKind::Lava) {
            return SDL_Color{247, 122, 57, 255};
        }
        return SDL_Color{196, 247, 97, 255};
    }

    bool is_static() const override { return true; }

    void tick(World& world, int x, int y, uint64_t frame, std::mt19937_64& rng) override {
        // Kinematic generators move periodically; they all start with rightward drift.
        if ((frame % static_cast<uint64_t>(move_interval_)) == 0ULL) {
            int nx = x + vx_;
            const int ny = y + vy_;

            // Reflect only on lateral borders.
            if (nx <= 0 || nx >= world.width() - 1) {
                vx_ = -vx_;
                nx = x + vx_;
            }

            if (world.in_bounds(nx, ny)) {
                const EntityType target = world.at(nx, ny).type();
                if (target != EntityType::BlackHole) {
                    // Force-occupy next cell to avoid jittery collision ping-pong.
                    world.swap_cells(x, y, nx, ny);
                    world.set(x, y, make_entity(EntityType::VoidCell));
                    world.at(nx, ny).last_update_frame = frame;
                }
            }
        }

        ++counter_;
        if (counter_ % interval_ != 0) {
            return;
        }

        std::vector<std::pair<int, int>> candidates;
        for (int dx = -1; dx <= 1; ++dx) {
            const int nx = x + dx;
            const int ny = y + 1;
            if (!world.in_bounds(nx, ny)) {
                continue;
            }
            if (world.at(nx, ny).type() == EntityType::VoidCell) {
                candidates.emplace_back(nx, ny);
            }
        }
        if (candidates.empty()) {
            return;
        }

        std::uniform_int_distribution<size_t> pick(0, candidates.size() - 1);
        auto [gx, gy] = candidates[pick(rng)];
        if (spawn_ == SpawnKind::Water) {
            world.set(gx, gy, make_entity(EntityType::Water));
        } else if (spawn_ == SpawnKind::Lava) {
            world.set(gx, gy, make_entity(EntityType::Lava));
        } else {
            world.set(gx, gy, make_entity(EntityType::Acid));
        }
        world.at(gx, gy).last_update_frame = frame;
    }

private:
    SpawnKind spawn_;
    int interval_;
    int move_interval_;
    int vx_;
    int vy_;
    int counter_ = 0;
};

class BlackHole final : public Entity {
public:
    EntityType type() const override { return EntityType::BlackHole; }
    SDL_Color color() const override { return SDL_Color{160, 64, 220, 255}; }
    bool is_static() const override { return false; }

    void tick(World& world, int x, int y, uint64_t frame, std::mt19937_64& rng) override {
        if (!initialized_) {
            std::uniform_int_distribution<int> sign(0, 1);
            vx_ = sign(rng) == 0 ? 1 : -1;
            vy_ = sign(rng) == 0 ? 1 : -1;
            initialized_ = true;
        }

        int cx = x;
        int cy = y;

        if ((frame % static_cast<uint64_t>(move_interval_)) == 0ULL) {
            int tx = x + vx_;
            int ty = y + vy_;

            if (tx <= 0 || tx >= world.width() - 1) {
                vx_ = -vx_;
                tx = x + vx_;
            }
            if (ty <= 0 || ty >= world.height() - 1) {
                vy_ = -vy_;
                ty = y + vy_;
            }

            if (world.in_bounds(tx, ty) && tx > 0 && tx < world.width() - 1 && ty > 0 &&
                ty < world.height() - 1) {
                EntityType tt = world.at(tx, ty).type();
                if (tt != EntityType::Generator && tt != EntityType::BlackHole &&
                    tt != EntityType::VoidCell) {
                    world.set(tx, ty, make_entity(EntityType::VoidCell));
                    world.at(tx, ty).last_update_frame = frame;
                    tt = EntityType::VoidCell;
                }

                if (tt == EntityType::VoidCell && world.try_move(x, y, tx, ty, frame)) {
                    cx = tx;
                    cy = ty;
                }
            }
        }

        for (int iy = cy - radius_; iy <= cy + radius_; ++iy) {
            for (int ix = cx - radius_; ix <= cx + radius_; ++ix) {
                if (!world.in_bounds(ix, iy)) {
                    continue;
                }
                if (ix <= 0 || iy <= 0 || ix >= world.width() - 1 || iy >= world.height() - 1) {
                    continue;
                }
                const int dx = ix - cx;
                const int dy = iy - cy;
                if (dx * dx + dy * dy > radius_ * radius_) {
                    continue;
                }
                if (ix == cx && iy == cy) {
                    continue;
                }
                const EntityType t = world.at(ix, iy).type();
                if (t != EntityType::VoidCell && t != EntityType::BlackHole &&
                    t != EntityType::Generator) {
                    world.set(ix, iy, make_entity(EntityType::VoidCell));
                    world.at(ix, iy).last_update_frame = frame;
                }
            }
        }
    }

private:
    bool initialized_ = false;
    int vx_ = 1;
    int vy_ = 1;
    int move_interval_ = 4;
    int radius_ = 3;
};

size_t entity_storage_size() {
    const std::array<size_t, 10> sizes = {
        sizeof(VoidCell),
        sizeof(Wall),
        sizeof(Sand),
        sizeof(Water),
        sizeof(Plant),
        sizeof(Lava),
        sizeof(Steam),
        sizeof(Acid),
        sizeof(Generator),
        sizeof(BlackHole),
    };
    return *std::max_element(sizes.begin(), sizes.end());
}

size_t entity_storage_align() {
    const std::array<size_t, 10> aligns = {
        alignof(VoidCell),
        alignof(Wall),
        alignof(Sand),
        alignof(Water),
        alignof(Plant),
        alignof(Lava),
        alignof(Steam),
        alignof(Acid),
        alignof(Generator),
        alignof(BlackHole),
    };
    return *std::max_element(aligns.begin(), aligns.end());
}

void World::construct_from_recipe(void* dst, const EntityRecipe& recipe) {
    switch (recipe.type) {
    case EntityType::VoidCell:
        std::construct_at(static_cast<VoidCell*>(dst));
        return;
    case EntityType::Wall:
        std::construct_at(static_cast<Wall*>(dst));
        return;
    case EntityType::Sand:
        std::construct_at(static_cast<Sand*>(dst));
        return;
    case EntityType::Water:
        std::construct_at(static_cast<Water*>(dst));
        return;
    case EntityType::Plant:
        std::construct_at(static_cast<Plant*>(dst));
        return;
    case EntityType::Lava:
        std::construct_at(static_cast<Lava*>(dst));
        return;
    case EntityType::Steam:
        std::construct_at(static_cast<Steam*>(dst), recipe.steam_life);
        return;
    case EntityType::Acid:
        std::construct_at(static_cast<Acid*>(dst));
        return;
    case EntityType::Generator:
        std::construct_at(static_cast<Generator*>(dst), recipe.spawn_kind, recipe.interval);
        return;
    case EntityType::BlackHole:
        std::construct_at(static_cast<BlackHole*>(dst));
        return;
    }
}

void World::destroy_entity(Entity& entity) {
    switch (entity.type()) {
    case EntityType::VoidCell:
        std::destroy_at(static_cast<VoidCell*>(&entity));
        return;
    case EntityType::Wall:
        std::destroy_at(static_cast<Wall*>(&entity));
        return;
    case EntityType::Sand:
        std::destroy_at(static_cast<Sand*>(&entity));
        return;
    case EntityType::Water:
        std::destroy_at(static_cast<Water*>(&entity));
        return;
    case EntityType::Plant:
        std::destroy_at(static_cast<Plant*>(&entity));
        return;
    case EntityType::Lava:
        std::destroy_at(static_cast<Lava*>(&entity));
        return;
    case EntityType::Steam:
        std::destroy_at(static_cast<Steam*>(&entity));
        return;
    case EntityType::Acid:
        std::destroy_at(static_cast<Acid*>(&entity));
        return;
    case EntityType::Generator:
        std::destroy_at(static_cast<Generator*>(&entity));
        return;
    case EntityType::BlackHole:
        std::destroy_at(static_cast<BlackHole*>(&entity));
        return;
    }
}

EntityRecipe make_entity(EntityType t) {
    EntityRecipe recipe;
    recipe.type = t;
    return recipe;
}

EntityRecipe make_generator(SpawnKind kind, int interval) {
    EntityRecipe recipe;
    recipe.type = EntityType::Generator;
    recipe.spawn_kind = kind;
    recipe.interval = interval;
    return recipe;
}

static int randint(std::mt19937_64& rng, int lo, int hi) {
    std::uniform_int_distribution<int> dist(lo, hi);
    return dist(rng);
}

static bool is_solid_support(EntityType t) {
    return t == EntityType::Wall || t == EntityType::Sand || t == EntityType::Plant;
}

static bool can_place_cluster(const World& world, int x, int y, int margin) {
    return x >= margin && y >= margin && x < world.width() - margin && y < world.height() - margin;
}

static void place_wall_vein(World& world, int sx, int sy, int steps, std::mt19937_64& rng) {
    static const std::array<std::pair<int, int>, 8> dirs = {
        std::pair{1, 0},
        std::pair{-1, 0},
        std::pair{0, 1},
        std::pair{0, -1},
        std::pair{1, 1},
        std::pair{-1, 1},
        std::pair{1, -1},
        std::pair{-1, -1},
    };
    std::uniform_real_distribution<double> prob(0.0, 1.0);

    struct Walker {
        int x;
        int y;
        int dir_index;
        int life;
    };

    std::vector<Walker> walkers;
    walkers.push_back({sx, sy, randint(rng, 0, static_cast<int>(dirs.size()) - 1), steps});

    while (!walkers.empty()) {
        Walker w = walkers.back();
        walkers.pop_back();

        while (w.life-- > 0) {
            if (!can_place_cluster(world, w.x, w.y, 1)) {
                break;
            }

            world.set(w.x, w.y, make_entity(EntityType::Wall));
            if (prob(rng) < 0.22) {
                for (int oy = -1; oy <= 1; ++oy) {
                    for (int ox = -1; ox <= 1; ++ox) {
                        if ((ox == 0 && oy == 0) || !world.in_bounds(w.x + ox, w.y + oy)) {
                            continue;
                        }
                        if (prob(rng) < 0.25) {
                            world.set(w.x + ox, w.y + oy, make_entity(EntityType::Wall));
                        }
                    }
                }
            }

            if (prob(rng) < 0.24) {
                const int delta = randint(rng, -3, 3);
                w.dir_index = (w.dir_index + delta + static_cast<int>(dirs.size())) %
                              static_cast<int>(dirs.size());
            }

            if (prob(rng) < 0.09 && w.life > 16) {
                const int branch_dir =
                    (w.dir_index + randint(rng, -2, 2) + static_cast<int>(dirs.size())) %
                    static_cast<int>(dirs.size());
                walkers.push_back({w.x, w.y, branch_dir, w.life / 2});
            }

            w.x += dirs[static_cast<size_t>(w.dir_index)].first;
            w.y += dirs[static_cast<size_t>(w.dir_index)].second;
        }
    }
}

static void place_sand_heap(World& world, int cx, int cy, int radius, std::mt19937_64& rng) {
    std::uniform_real_distribution<double> prob(0.0, 1.0);
    const int r2 = radius * radius;
    for (int y = std::max(1, cy - radius); y <= std::min(world.height() - 2, cy + radius); ++y) {
        for (int x = std::max(1, cx - radius); x <= std::min(world.width() - 2, cx + radius); ++x) {
            const int dx = x - cx;
            const int dy = y - cy;
            const int d2 = dx * dx + dy * dy;
            if (d2 > r2) {
                continue;
            }
            const double falloff = 1.0 - static_cast<double>(d2) / std::max(1, r2);
            if (prob(rng) < 0.18 + 0.74 * falloff) {
                world.set(x, y, make_entity(EntityType::Sand));
            }
        }
    }
}

static void grow_plants_constrained(
    World& world, int sx, int sy, int budget, std::mt19937_64& rng
) {
    std::uniform_real_distribution<double> prob(0.0, 1.0);
    std::vector<std::pair<int, int>> frontier;
    if (world.in_bounds(sx, sy)) {
        frontier.emplace_back(sx, sy);
        world.set(sx, sy, make_entity(EntityType::Plant));
    }

    static const std::array<std::pair<int, int>, 8> dirs = {
        std::pair{1, 0},
        std::pair{-1, 0},
        std::pair{0, 1},
        std::pair{0, -1},
        std::pair{1, 1},
        std::pair{-1, 1},
        std::pair{1, -1},
        std::pair{-1, -1},
    };

    while (!frontier.empty() && budget-- > 0) {
        const size_t i =
            static_cast<size_t>(randint(rng, 0, static_cast<int>(frontier.size()) - 1));
        auto [x, y] = frontier[i];
        frontier[i] = frontier.back();
        frontier.pop_back();

        for (const auto& [dx, dy] : dirs) {
            const int nx = x + dx;
            const int ny = y + dy;
            if (!can_place_cluster(world, nx, ny, 1)) {
                continue;
            }
            if (world.at(nx, ny).type() != EntityType::VoidCell) {
                continue;
            }

            const bool near_core = std::abs(nx - sx) + std::abs(ny - sy) < budget / 6 + 14;
            const bool has_support = is_solid_support(world.at(nx, ny + 1).type()) ||
                                     is_solid_support(world.at(nx, ny - 1).type());
            if (!has_support && prob(rng) > 0.16) {
                continue;
            }

            const double p = near_core ? 0.58 : 0.26;
            if (prob(rng) < p) {
                world.set(nx, ny, make_entity(EntityType::Plant));
                frontier.emplace_back(nx, ny);
            }
        }
    }
}

static void place_fluid_pool(
    World& world, EntityType t, int cx, int cy, int size, std::mt19937_64& rng
) {
    std::uniform_real_distribution<double> prob(0.0, 1.0);
    std::vector<std::pair<int, int>> q;
    q.emplace_back(cx, cy);

    static const std::array<std::pair<int, int>, 8> dirs = {
        std::pair{1, 0},
        std::pair{-1, 0},
        std::pair{0, 1},
        std::pair{0, -1},
        std::pair{1, 1},
        std::pair{-1, 1},
        std::pair{1, -1},
        std::pair{-1, -1},
    };

    int placed = 0;
    while (!q.empty() && placed < size) {
        auto [x, y] = q.back();
        q.pop_back();
        if (!can_place_cluster(world, x, y, 1)) {
            continue;
        }
        EntityType cur = world.at(x, y).type();
        if (cur != EntityType::VoidCell && cur != EntityType::Sand && cur != EntityType::Plant) {
            continue;
        }

        world.set(x, y, make_entity(t));
        ++placed;

        for (const auto& [dx, dy] : dirs) {
            const int nx = x + dx;
            const int ny = y + dy;
            if (!world.in_bounds(nx, ny)) {
                continue;
            }
            if (prob(rng) < 0.57) {
                q.emplace_back(nx, ny);
            }
        }
    }
}

void seed_world(World& world, uint64_t seed) {
    std::mt19937_64 rng(seed);

    for (int y = 0; y < world.height(); ++y) {
        for (int x = 0; x < world.width(); ++x) {
            if (x == 0 || y == 0 || x == world.width() - 1 || y == world.height() - 1) {
                world.set(x, y, make_entity(EntityType::Wall));
            } else {
                world.set(x, y, make_entity(EntityType::VoidCell));
            }
        }
    }

    const int area = world.width() * world.height();
    const int wall_cores = std::max(4, area / 4200);
    const int sand_cores = std::max(3, area / 5200);
    const int plant_cores = std::max(3, area / 5600);
    const int water_cores = std::max(2, area / 9000);
    const int lava_cores = std::max(1, area / 16000);

    struct Core {
        EntityType type;
        int x;
        int y;
    };
    std::vector<Core> cores;

    for (int i = 0; i < wall_cores; ++i) {
        cores.push_back(
            {EntityType::Wall,
             randint(rng, 6, world.width() - 7),
             randint(rng, 5, world.height() - 6)}
        );
    }
    for (int i = 0; i < sand_cores; ++i) {
        cores.push_back(
            {EntityType::Sand,
             randint(rng, 8, world.width() - 9),
             randint(rng, world.height() / 3, world.height() - 8)}
        );
    }
    for (int i = 0; i < plant_cores; ++i) {
        cores.push_back(
            {EntityType::Plant,
             randint(rng, 8, world.width() - 9),
             randint(rng, world.height() / 2, world.height() - 7)}
        );
    }
    for (int i = 0; i < water_cores; ++i) {
        cores.push_back(
            {EntityType::Water,
             randint(rng, 8, world.width() - 9),
             randint(rng, 6, world.height() / 2)}
        );
    }
    for (int i = 0; i < lava_cores; ++i) {
        cores.push_back(
            {EntityType::Lava,
             randint(rng, 8, world.width() - 9),
             randint(rng, world.height() * 2 / 3, world.height() - 8)}
        );
    }
    std::shuffle(cores.begin(), cores.end(), rng);

    for (const auto& c : cores) {
        if (c.type == EntityType::Wall) {
            const int steps = randint(rng, 18, 50);
            place_wall_vein(world, c.x, c.y, steps, rng);
        }
    }

    for (const auto& c : cores) {
        if (c.type == EntityType::Sand) {
            place_sand_heap(world, c.x, c.y, randint(rng, 7, 15), rng);
        }
    }

    for (const auto& c : cores) {
        if (c.type == EntityType::Plant) {
            grow_plants_constrained(world, c.x, c.y, randint(rng, 220, 520), rng);
        }
    }

    for (const auto& c : cores) {
        if (c.type == EntityType::Water) {
            place_fluid_pool(world, EntityType::Water, c.x, c.y, randint(rng, 24, 64), rng);
        } else if (c.type == EntityType::Lava) {
            place_fluid_pool(world, EntityType::Lava, c.x, c.y, randint(rng, 9, 28), rng);
        }
    }

    const int top = 2;
    const int bottom = world.height() - 3;

    const int acid_y = std::min(world.height() - 13, top + 1);
    const int water_y = acid_y + 5;
    const int lava_y = acid_y + 10;

    const int acid_x = std::max(2, world.width() / 6);
    const int water_x = world.width() / 2;
    const int lava_x = std::min(world.width() - 3, world.width() * 5 / 6);

    world.set(acid_x, acid_y, make_generator(SpawnKind::Acid, 7));
    world.set(water_x, water_y, make_generator(SpawnKind::Water, 4));
    world.set(lava_x, lava_y, make_generator(SpawnKind::Lava, 7));

    world.set(world.width() / 2, bottom, make_entity(EntityType::BlackHole));
}

void render_world(SDL_Renderer* renderer, const World& world, int cell_size) {
    SDL_SetRenderDrawColor(renderer, 8, 8, 12, 255);
    SDL_RenderClear(renderer);

    SDL_Rect rect{0, 0, cell_size, cell_size};
    for (int y = 0; y < world.height(); ++y) {
        for (int x = 0; x < world.width(); ++x) {
            SDL_Color c = world.at(x, y).color();
            SDL_SetRenderDrawColor(renderer, c.r, c.g, c.b, c.a);
            rect.x = x * cell_size;
            rect.y = y * cell_size;
            SDL_RenderFillRect(renderer, &rect);
        }
    }

    SDL_RenderPresent(renderer);
}

int run_visualization(int width, int height, uint64_t seed) {
    constexpr int kCellSize = 8;
    constexpr uint32_t kFrameMs = 1000 / 24;

    SDL_Init(SDL_INIT_VIDEO);

    SDL_Window* window = SDL_CreateWindow(
        "Polymorphism Sandbox",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        width * kCellSize,
        height * kCellSize,
        SDL_WINDOW_SHOWN
    );

    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if (renderer == nullptr) {
        renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    }

    std::mt19937_64 rng(seed);
    World world(width, height);
    seed_world(world, seed);

    bool running = true;
    uint64_t frame = 1;
    while (running) {
        const uint32_t frame_start = SDL_GetTicks();

        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                running = false;
            }
            if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE) {
                running = false;
            }
        }

        world.step(frame, rng);
        ++frame;
        render_world(renderer, world, kCellSize);

        const uint32_t elapsed = SDL_GetTicks() - frame_start;
        if (elapsed < kFrameMs) {
            SDL_Delay(kFrameMs - elapsed);
        }
    }

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}

int run_benchmark(int width, int height, uint64_t seed, uint64_t steps) {
    World world(width, height);
    seed_world(world, seed);
    std::mt19937_64 rng(seed ^ 0x9E3779B97F4A7C15ULL);

    const auto start = std::chrono::steady_clock::now();
    for (uint64_t frame = 1; frame <= steps; ++frame) {
        world.step(frame, rng);
    }
    const auto end = std::chrono::steady_clock::now();
    const auto elapsed_ns =
        std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();

    const double elapsed_s = static_cast<double>(elapsed_ns) / 1e9;
    const double fps_logic = static_cast<double>(steps) / std::max(1e-9, elapsed_s);

    std::cout << "benchmark_mode=on\n";
    std::cout << "steps=" << steps << "\n";
    std::cout << "elapsed_sec=" << elapsed_s << "\n";
    std::cout << "logic_fps=" << fps_logic << "\n";
    std::cout << "remaining_water=" << world.count_type(EntityType::Water) << "\n";
    std::cout << "remaining_lava=" << world.count_type(EntityType::Lava) << "\n";
    std::cout << "remaining_steam=" << world.count_type(EntityType::Steam) << "\n";
    return 0;
}

int main(int argc, char** argv) {
    int width = 160;
    int height = 96;
    uint64_t seed = 1337;
    bool benchmark = false;
    uint64_t steps = 5000;

    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg == "--benchmark") {
            benchmark = true;
            if (i + 1 < argc) {
                steps = static_cast<uint64_t>(std::strtoull(argv[i + 1], nullptr, 10));
                ++i;
            }
        } else if (arg == "--seed" && i + 1 < argc) {
            seed = static_cast<uint64_t>(std::strtoull(argv[++i], nullptr, 10));
        } else if (arg == "--width" && i + 1 < argc) {
            width = std::max(32, std::atoi(argv[++i]));
        } else if (arg == "--height" && i + 1 < argc) {
            height = std::max(32, std::atoi(argv[++i]));
        }
    }

    if (benchmark) {
        return run_benchmark(width, height, seed, steps);
    }
    return run_visualization(width, height, seed);
}
